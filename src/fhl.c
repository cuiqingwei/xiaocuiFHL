/******************************************************************************
 * fhl.c - fhl
 * 
 * Copyright 1998-2003 Routon Technology Co.,Ltd.
 * 
 * DESCRIPTION: - 
 *    fhl驱动
 * modification history
 * --------------------
 * 01a, 15.01.2007, cuiqingwei written
 * --------------------
 ******************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>

#include <util/delay.h>
/*----------------*
sleep.h里面定义的常数，对应各种睡眠模式
#define SLEEP_MODE_IDLE         0   							空闲模式
#define SLEEP_MODE_ADC          _BV(SM0)  						ADC 噪声抑制模式
#define SLEEP_MODE_PWR_DOWN     _BV(SM1)						掉电模式
#define SLEEP_MODE_PWR_SAVE     (_BV(SM0)|_BV(SM1))				省电模式
#define SLEEP_MODE_STANDBY      (_BV(SM1)|_BV(SM2))				Standby 模式
#define SLEEP_MODE_EXT_STANDBY  (_BV(SM0)|_BV(SM1)|_BV(SM2))	扩展Standby模式
//
void set_sleep_mode (uint8_t mode);		设定睡眠模式
void sleep_mode (void);					进入睡眠状态
 *----------------*/

/*----------------*
 *	  时钟主频    *
 *----------------*/
#ifndef F_CPU
#define F_CPU 7372800UL
#endif
/*----------------*
 *	  端口定义    *
 *----------------*/

#define BOTTON_PIN 				PIND
#define bitBOTTON 				2
#define HALL_PORT 				PORTD
#define HALL_PIN 				PIND
#define bitHALLOUT 				3
#define bitHALLPWR  			6

/*----------------*
 *	 74HC595引脚  *
 *----------------*/
#define SEL_SIDE 				PORTD				// 锁存时钟--74HC595 12# 
#define bitFRONT 				4
#define bitBACK 				5
#define SET_SER 				PORTB|= _BV(PB4)	// 数据------74HC595 14# 
#define CLR_SER 				PORTB&=~_BV(PB4)	
#define SET_SCLK 				PORTB|= _BV(PB5)	// 移位时钟--74HC595 11# 
#define CLR_SCLK 				PORTB&=~_BV(PB5)

/*----------------*
 *	   宏定义     *
 *----------------*/
#define EN_ANIMATE 				1					// 支持动画

#define NUM_PIXELS 				256 				// 像素数256 (刚好一个字节)
#define NUM_LEDS 				32 					// 单面LED数据

#define HALL_DEBOUNCE 			4  					// 
#define BUTTON_DEBOUNCE  		100					// 100ms

#define STANDBY_TIMEOUT 		5*((F_CPU/256)/0xff)// F_CPU晶振频率 约5S
#define POWEROFF_TIMEOUT 		2*60*((F_CPU/256)/0xff) 

/*----------------*
 *	  EE地址定义  *
 *----------------*/
#ifndef EEWE
#define EEWE 					1
#endif
#ifndef EEMWE
#define EEMWE 					2
#endif

#define E2_ROTATION_OFFSET 		0x00
#define E2_MIRROR 				0x01				// 镜相
#define E2_ANIMATION 			0x02				// 动画
/*----------------*
 *	  变量定义    *
 *----------------*/
volatile unsigned int  tLap; 						// 轮子转一圈所用时间存放变量 
volatile unsigned char tHall;						// 霍尔传感器检测时间

unsigned char mirror;								// 镜相
unsigned char fleds[4], bleds[4];  					// 前、后面LED对应缓�

unsigned char tanim_dalay    = 6;					// 动画保持
volatile unsigned int tanim  = 0;  					// 时间控制变量

volatile unsigned int anim_e2_offset= 0;
volatile unsigned int curr_e2_addr  = 0;			// 

/*----------------*
 *	  图像数据    *
 *----------------*/
unsigned char val[] EEMEM ={						// 存入E2 EEMEM 等同于 __attribute__((section(".eeprom"))) 
	"string in eeprom"};

/*----------------*
 *	  函数声名    *
 *----------------*/
void cpu_init	 (void);
void refresh_leds(void);
void ctrl595_out (unsigned char);
void set_led	 (unsigned char , unsigned char );  
void inter_eeprom_write	(unsigned char , unsigned char );
unsigned char inter_eeprom_read	(unsigned char ); 
unsigned char spi_transfer		(unsigned char ); 
/*----------------------------------------------*
 *					CPU初始化                   *
 *----------------------------------------------*/
void cpu_init(void) 
{
	/* 端口配置 */
  	PORTB = 0xCF;
 	DDRB  = 0x30;
 	PORTC = 0x7F; 				//　m103 output only
 	DDRC  = 0x00;
 	PORTD = 0x8F;
 	DDRD  = 0x72;


  	PORTD |= _BV(bitHALLPWR);	// 打开霍尔传感器  
  	
        
	cli();

 	MCUCR = 0x00;
 
	/* 外部中断0、1低电平触发，0:按键；1:传感器 */
  	EICRA  = _BV(ISC11) & ~_BV(ISC10) &~_BV(ISC01) &  ~_BV(ISC00);
  	/* 打开外部中断 */
  	EIMSK  = _BV(INT1) | _BV(INT0);
  	EIFR   = ~_BV(INTF1)& ~_BV(INTF0); 
	
	/* 定时器0 */
  	TCCR0A = 0;				
  	TCNT0  = 0xff;			// 100us
  	TCCR0B = _BV(CS02); 	// 256分频 clk/256 
  	TIMSK0|= _BV(TOIE0); 	// 打开溢出中断
  
  	/* 定时器1 CTC模式 */
  	TCCR1A = 0;
  	TCCR1B = _BV(WGM12); 	// WGM: 4) CTC, TOP=OCRnA

	/* 看门狗 */
 	wdt_enable(WDTO_1S);

	sei();
}

/*----------------------------------------------*
 *	      外部中断0:按键检测(低电平)            *
 *----------------------------------------------*/
SIGNAL (INT0_vect) 
{
  	unsigned int t=0;

  	while (!(BOTTON_PIN & _BV(bitBOTTON)))	// 等待释放按键 
	{
    	wdt_reset();
		if(++t>0xfff0);
			t = 0xfff0;
    	_delay_ms(1);
  	}
  	if (t > BUTTON_DEBOUNCE) 
	{
    	if (t < 500) 
		{
      		/* 短按键，小于500ms 打开看门狗死循环造成复位，激活系统 */
      		wdt_enable(WDTO_250MS);	// 使能看门狗
      		while (1);
   		} 
		else	// 长按键休眠待机
      		tLap = 0xffff;
  	}
}
/*----------------------------------------------*
 *	     外部中断1:霍尔传感器触发(低电平)       *
 *----------------------------------------------*/
SIGNAL (INT1_vect) 
{
  	if (tHall > HALL_DEBOUNCE) 				// 软抗骚扰，防止误触发
	{
#if EN_ANIMATE								// 动画支持
  		if (tanim != tanim_dalay) 
		{
    		tanim++;
  		} 
		else 
		{
    		tanim = 0;
    		anim_e2_offset += 1024;
  		}
#endif
		/* 我们知道最后一次霍尔传感器触发后的毫秒数，
		以及每次扫描有256条射线状“像素” 以便分别在
		每个像素间断期间取得必要的毫秒数，现在仅仅使
		得定时器1处于那个比率，也就是T/C1中断时间是
		T0的256之1，T0所选时钟256分频，则T/C1所选时
		时钟不分频*/

    	TCNT1 = 0;								// timer1计数值清0
    	if ((tLap < 0xff) && (tLap > 0x3)) 		// 时间在短太长都不处理
		{
      		OCR1A = (tLap << 8) | TCNT0;		// 输了比较寄存器赋值
      		TCNT0 = 0;							// T0计数值清零
      		
      		/* 取出源数据存放地址 */
      		curr_e2_addr = (inter_eeprom_read(E2_ROTATION_OFFSET) % NUM_PIXELS)* 4;
      		mirror = inter_eeprom_read(E2_MIRROR);
      		
			TCCR1B |= _BV(CS10);                // 时钟无分频
      		TIMSK1 |= _BV(OCIE1A);				// T/C1输出比较A产生中断
   	 	} 
		else 
		{
      		set_led(0, bitFRONT);
      		set_led(0, bitBACK);
      		TCCR1B &= ~_BV(CS10);				// 无时钟源，停止T/C1
    	}   
    	tLap = 0;
  	}
  	tHall = 0;
}

/*----------------------------------------------*
 *			   T0 100u定时中断                  *
 *----------------------------------------------*/
SIGNAL (TIMER0_OVF_vect) 
{
  	if (tHall != 0xff)
    	tHall++;
  
  	if (tLap != 0xffff)
    	tLap++;
}

/*----------------------------------------------*
 *		T/C1 CTC(比较匹配时清零定时器)模式      *
 *     ------------------------------------     *
 * 数器数值TCNT0一直累加到TCNT0与OCR0A匹配,然后 * 
 * TCNT0 清零.产生此中断                        *
 *----------------------------------------------*/
SIGNAL (TIMER1_COMPA_vect) 
{
  	unsigned int addr;

  	sei(); 

  	addr = curr_e2_addr;			// 源数据地址

  	if (tLap < STANDBY_TIMEOUT) 	// 转一圈地时间小于5S才显示
  	{    
    	addr %= (NUM_PIXELS * 4);
    
    	ctrl595_out(inter_eeprom_read(addr + anim_e2_offset));// 要显示数据送出

  		SEL_SIDE |= _BV(bitFRONT);
  		asm("nop"); asm("nop"); 
  		asm("nop"); asm("nop");
  		SEL_SIDE &=~_BV(bitFRONT);
  
    	if (mirror) 
    	{
      		ctrl595_out(inter_eeprom_read(anim_e2_offset + (1024UL-addr)));

  			SEL_SIDE |= _BV(bitBACK);
  			asm("nop"); asm("nop");
  			asm("nop"); asm("nop");
  			SEL_SIDE &=~_BV(bitBACK);
    	} 
    	else 
    	{
      		SEL_SIDE |= _BV(bitBACK);
      		asm("nop"); asm("nop"); 
      		asm("nop"); asm("nop");
      		SEL_SIDE &= ~_BV(bitBACK);
    	}
    	/*
    	fleds[0] = fleds[1] = fleds[2] = fleds[3] = 0xFF;
    	fleds[addr / 8] = ~(_BV(addr % 8));
    	clock_leds(bitFRONT);
    	addr++;
    	if (addr > 32)
      		addr = 0;
    	*/
    	/* 关中断，确保在同一时间其它中断打断写入此地址 */
    	cli();
    	if (addr == (curr_e2_addr%(NUM_PIXELS*4))) 
    	{
      		curr_e2_addr = addr+4;	// 地址指针移向下一数据区
      	}
      	//curr_e2_addr = addr;
    	sei();
  	} 
  	else 	// 转一圈超过5S
  	{
    	cli();
    	TCCR1B &= ~0x7;				// 关闭T/C1
    	sei();
    	set_led(0, bitFRONT);		// turn off all but one LED;
    	set_led(0, bitBACK);
  	}
}

/*----------------------------------------------*
 *				  595输出控制                   *
 *----------------------------------------------*/
void ctrl595_out(unsigned char sel) 
{ 
	unsigned int i; 
	unsigned char *leds;
	unsigned long dat;

  	if (sel == bitFRONT)
    	leds = fleds;			// 前面
  	else
    	leds = bleds;   		// 后面

	dat = ((long)leds[3]<<24)|((long)leds[2]<<16)|((long)leds[1]<<8)|leds[0];
	dat = ~dat;

	for (i=0;i<NUM_LEDS;i++) 
   	{  		
		CLR_SCLK;				// 移位时钟置低 
    	if (dat&1) 
			SET_SER;			// 置高数据时钟
		else 
			CLR_SER;  			// 接低数据时钟		
    	dat>>=1; 				//    __
    	SET_SCLK;				// __↑   移位时钟上升沿,数据进入移位寄存器   
    } 

	SEL_SIDE &=~_BV(sel);
  	asm("nop"); asm("nop"); 
  	asm("nop"); asm("nop");		//    __
  	SEL_SIDE |= _BV(sel);		// __↑   锁存电平上升沿，数据输出到并行端口  
} 

/*----------------------------------------------*
 *				    LEDS 控制                   *
 *----------------------------------------------*/
void set_led(unsigned char led, unsigned char side) 
{
  	unsigned char *leds;

  	if (side == bitFRONT)
    	leds = fleds;
  	else
    	leds = bleds;

  	leds[0] = leds[1] = leds[2] = leds[3] = 0xff;

  	leds[led/8] =~_BV(led%8);

  	ctrl595_out(side);
}

/*----------------------------------------------*
 *				    LEDS 刷新                   *
 *----------------------------------------------*/
void refresh_leds(void) 
{
  	unsigned char i;

  	for(i=0; i< NUM_LEDS; i++) 
	{
    	set_led(i, bitFRONT);
    	set_led(NUM_LEDS-i, bitBACK);
    	_delay_ms(50);
  	}
}

/*----------------------------------------------*
 *				    读内部eeprom                *
 *----------------------------------------------*/
unsigned char inter_eeprom_read(unsigned char addr) 
{
 	loop_until_bit_is_clear(EECR, EEWE);// 等待写完成
  	EEAR = addr;
  	EECR |= _BV(EERE);        			// 开始读EEPROM
  	return EEDR;            			// 返回仅1个时钟周期
}

/*----------------------------------------------*
 *				    写内部eeprom                *
 *----------------------------------------------*/
void inter_eeprom_write(unsigned char addr, unsigned char data) 
{
  	loop_until_bit_is_clear(EECR, EEWE);// 等待写完成
  	EEAR = addr;
  	EEDR = data;
  	cli();                				// 关闭所有中断
  	EECR |= _BV(EEMWE);   				// 此操作必须在4个时钟周期内发生
  	EECR |= _BV(EEWE);
  	sei();                				// 打开中断
}

int main (void)
{
	cpu_init(); 

	//led[0] = 0x01;
	//led[1] = 0x80;
	//led[2] = 0x55;
	//led[3] = 0x80;

  	tHall = 0;
  	tLap  = 0;

	while(1)
	{	
		wdt_reset();

		if (tLap == 0xffff)
		{
      		cli();
     		/* 关闭的有LED及霍尔传感器 */
      		HALL_PORT &=~_BV(bitHALLPWR);

      		/* 关闭看门狗 */
      		wdt_disable();

      		/* 休眠待机 */
			set_sleep_mode(SLEEP_MODE_PWR_SAVE);	// 省电模式
			sleep_mode();

			sei();
    	}

	}
}
/*----------------------------------END OF FILE-------------------------------*/

