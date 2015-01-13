/******************************************************************************
 * fhl.c - ���뻷��AVRGCC
 * 
 * Copyright 1998-2003 Routon Technology Co.,Ltd.
 * 
 * DESCRIPTION: - 
 *    ����ֿ���Դ��
 * modification history
 * --------------------
 * 01a, 16.01.2007, cuiqingwei written
 * --------------------
 ******************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
//#include <avr/wdt.h>
//#include <avr/eeprom.h>
//#include <avr/pgmspace.h> 
#include <util/delay.h>

/*----------------*
 *	  �˿ڶ���    *
 *----------------*/

#define BOTTON_PIN 				PIND
#define bitBOTTON 				2
#define HALL_PORT 				PORTD
#define HALL_PIN 				PIND
#define bitHALLOUT 				3
#define bitHALLPWR  			6

/*----------------*
 *	 74HC595����  *
 *----------------*/
#define SEL_SIDE 				PORTD				// ����ʱ��--74HC595 12# 
#define bitFRONT 				4
#define bitBACK 				5
#define SET_SER 				PORTB|= _BV(PB4)	// ����------74HC595 14# 
#define CLR_SER 				PORTB&=~_BV(PB4)	
#define SET_SCLK 				PORTB|= _BV(PB5)	// ��λʱ��--74HC595 11# 
#define CLR_SCLK 				PORTB&=~_BV(PB5)

/*----------------*
 *	   �궨��     *
 *----------------*/
#define EN_ANIMATE 				1					// ֧�ֶ���

#define NUM_PIXELS 				256 				// ������256 (�պ�һ���ֽ�)
#define NUM_LEDS 				32 					// ����LED����

#define HALL_DEBOUNCE 			4  					// 
#define BUTTON_DEBOUNCE  		100					// 100ms

#define STANDBY_TIMEOUT 		5*((F_CPU/256)/0xff)// F_CPU����Ƶ�� Լ5S
#define POWEROFF_TIMEOUT 		2*60*((F_CPU/256)/0xff) 

/*----------------*
 *	  EE��ַ����  *
 *----------------*/
#ifndef EEWE
#define EEWE 					1
#endif
#ifndef EEMWE
#define EEMWE 					2
#endif

#define E2_ROTATION_OFFSET 		0x00
#define E2_MIRROR 				0x01				// ����
#define E2_ANIMATION 			0x02				// ����

/*----------------*
 *	  ��������    *
 *----------------*/
volatile unsigned int  tLap; 						// ����תһȦ����ʱ���ű��� 
volatile unsigned char tHall;						// �������������ʱ��

unsigned char mirror;								// ����
unsigned char fleds[4], bleds[4];  					// ǰ������LED��Ӧ���

unsigned char tanim_dalay    = 6;					// ��������
volatile unsigned int tanim  = 0;  					// ʱ����Ʊ���

volatile unsigned int anim_e2_offset= 0;
volatile unsigned int curr_e2_addr  = 0;			// 

/*----------------*
 *	  ͼ������    *
 *----------------*/
unsigned char val[] EEMEM ={						// ����E2 EEMEM ��ͬ�� __attribute__((section(".eeprom"))) 
	"string in eeprom"};

/*----------------*
 *	  ��������    *
 *----------------*/
 
void cpu_init	 (void);
void refresh_leds(void);
void ctrl595_out (unsigned char);
void set_led	 (unsigned char , unsigned char );  
void inter_eeprom_write	(unsigned char , unsigned char );
unsigned char inter_eeprom_read	(unsigned char ); 
unsigned char spi_transfer		(unsigned char ); 


/*----------------------------------------------*
 *					CPU��ʼ��                   *
 *----------------------------------------------*/
void cpu_init(void) 
{
	/* �˿����� */
 	PORTB = 0xC7;
 	DDRB  = 0x38;
 	PORTC = 0x7F; //m103 output only
 	DDRC  = 0x00;
 	PORTD = 0x8F;
 	DDRD  = 0x72;

  	PORTD = (_BV(bitBOTTON) | _BV(bitHALLOUT) | _BV(bitHALLPWR))  
    & ~_BV(bitFRONT) & ~_BV(bitBACK);  	
    
	/* ���Ź� */
	MCUSR  = 0;			// MCUSR �е�WDRF����
  	WDTCSR = _BV(WDE) | _BV(WDP2) | _BV(WDP1); // ʹ�� 1 S
  	
  	/* �ⲿ�ж�0��1�͵�ƽ������0:������1:������ */
  	EICRA  = _BV(ISC11) & ~_BV(ISC10) & _BV(ISC01) &  ~_BV(ISC00);

  	/* ���ⲿ�ж� */
  	EIMSK  = _BV(INT1) | _BV(INT0);
  	
	/* ��ʱ��0 */
  	TCCR0A = 0;				
  	TCNT0  = 0xff;			// 100us
  	TCCR0B = _BV(CS02); 	// 256��Ƶ clk/256 
  	TIMSK0|= _BV(TOIE0); 	// ������ж�
  
  	/* ��ʱ��1 CTCģʽ */
  	TCCR1A = 0;
  	TCCR1B = _BV(WGM12); 	// WGM: 4) CTC, TOP=OCRnA

  	tHall = 0;
  	tLap  = 0;
}

/*----------------------------------------------*
 *	      �ⲿ�ж�0:�������(�͵�ƽ)            *
 *----------------------------------------------*/
SIGNAL (INT0_vect) 
{
  	unsigned int t=0;

  	while (!(BOTTON_PIN & _BV(bitBOTTON)))	// �ȴ��ͷŰ��� 
	{
    	t++;
    	_delay_ms(1);
  	}
  	if (t > BUTTON_DEBOUNCE) 
	{
    	if (t < 500UL) 
		{
      		/* �̰�����С��500ms �򿪿��Ź���ѭ����ɸ�λ������ϵͳ */
      		WDTCSR = _BV(WDE);	// ʹ�ܿ��Ź�
      		while (1);
   		} 
		else	// ���������ߴ���
      		tLap = 0xffff;
  	}
}

/*----------------------------------------------*
 *	     �ⲿ�ж�1:��������������(�͵�ƽ)       *
 *----------------------------------------------*/
SIGNAL (INT1_vect) 
{
  	if (tHall > HALL_DEBOUNCE) 				// ��ɧ�ţ���ֹ�󴥷�
	{
#if EN_ANIMATE								// ����֧��
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
		/* ����֪�����һ�λ���������������ĺ�������
		�Լ�ÿ��ɨ����256������״�����ء� �Ա�ֱ���
		ÿ�����ؼ���ڼ�ȡ�ñ�Ҫ�ĺ����������ڽ���ʹ
		�ö�ʱ��1�����Ǹ����ʣ�Ҳ����T/C1�ж�ʱ����
		T0��256֮1��T0��ѡʱ��256��Ƶ����T/C1��ѡʱ
		ʱ�Ӳ���Ƶ*/

    	TCNT1 = 0;								// timer1����ֵ��0
    	if ((tLap < 0xff) && (tLap > 0x3)) 		// ʱ���ڶ�̫����������
		{
      		OCR1A = (tLap << 8) | TCNT0;		// ���˱ȽϼĴ�����ֵ
      		TCNT0 = 0;							// T0����ֵ����
      		
      		/* ȡ��Դ���ݴ�ŵ�ַ */
      		curr_e2_addr = (inter_eeprom_read(E2_ROTATION_OFFSET) % NUM_PIXELS)* 4;
      		mirror = inter_eeprom_read(E2_MIRROR);
      		
			TCCR1B |= _BV(CS10);                // ʱ���޷�Ƶ
      		TIMSK1 |= _BV(OCIE1A);				// T/C1����Ƚ�A�����ж�
   	 	} 
		else 
		{
      		set_led(2, bitFRONT);
      		set_led(2, bitBACK);
      		TCCR1B &= ~_BV(CS10);				// ��ʱ��Դ��ֹͣT/C1
    	}   
    	tLap = 0;
  	}
  	tHall = 0;
}

/*----------------------------------------------*
 *			   T0 100u��ʱ�ж�                  *
 *----------------------------------------------*/
SIGNAL (TIMER0_OVF_vect) 
{
  	if (tHall != 0xff)
    	tHall++;
  
  	if (tLap != 0xffff)
    	tLap++;
}

/*----------------------------------------------*
 *		T/C1 CTC(�Ƚ�ƥ��ʱ���㶨ʱ��)ģʽ      *
 *     ------------------------------------     *
 * ������ֵTCNT0һֱ�ۼӵ�TCNT0��OCR0Aƥ��,Ȼ�� * 
 * TCNT0 ����.�������ж�                        *
 *----------------------------------------------*/
SIGNAL (TIMER1_COMPA_vect) 
{
  	unsigned int addr;

  	sei(); 

  	addr = curr_e2_addr;			// Դ���ݵ�ַ

  	if (tLap < STANDBY_TIMEOUT) 	// תһȦ��ʱ��С��5S����ʾ
  	{    
    	addr %= (NUM_PIXELS * 4);
    
    	ctrl595_out(inter_eeprom_read(addr + anim_e2_offset));// Ҫ��ʾ�����ͳ�

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
    	/* ���жϣ�ȷ����ͬһʱ�������жϴ��д��˵�ַ */
    	cli();
    	if (addr == (curr_e2_addr%(NUM_PIXELS*4))) 
    	{
      		curr_e2_addr = addr+4;	// ��ַָ��������һ������
      	}
      	//curr_e2_addr = addr;
    	sei();
  	} 
  	else 	// תһȦ����5S
  	{
    	cli();
    	TCCR1B &= ~0x7;				// �ر�T/C1
    	sei();
    	set_led(2, bitFRONT);		// turn off all but one LED;
    	set_led(2, bitBACK);
  	}
}
/*----------------------------------------------*
 *				  595�������                   *
 *----------------------------------------------*/
 
void ctrl595_out(unsigned char sel) 
{ 
	unsigned int i; 
	unsigned char *leds;
	unsigned long dat;

  	if (sel == bitFRONT)
    	leds = fleds;			// ǰ��
  	else
    	leds = bleds;   		// ����

	dat = ((long)leds[3]<<24)|((long)leds[2]<<16)|((long)leds[1]<<8)|leds[0];

	for (i=0;i<NUM_LEDS;i++) 
   	{  		
		CLR_SCLK;				// ��λʱ���õ� 

    	if (dat&1) 
			SET_SER;			// �ø�����ʱ��
		else 
			CLR_SER;  			// �ӵ�����ʱ��		
    	dat>>=1; 

    	SET_SCLK;				// __��   ��λʱ��������,���ݽ�����λ�Ĵ���   
    } 

	SEL_SIDE |= _BV(sel);
  	asm("nop"); asm("nop"); 
  	asm("nop"); asm("nop");		//    __
  	SEL_SIDE &=~_BV(sel);		// __��   �����ƽ�����أ�������������ж˿�  
} 

/*----------------------------------------------*
 *				    LEDS ����                   *
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
 *				    LEDS ˢ��                   *
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
 *				    ���ڲ�eeprom                *
 *----------------------------------------------*/
unsigned char inter_eeprom_read(unsigned char addr) 
{
 	loop_until_bit_is_clear(EECR, EEWE);// �ȴ�д���
  	EEAR = addr;
  	EECR |= _BV(EERE);        			// ��ʼ��EEPROM
  	return EEDR;            			// ���ؽ�1��ʱ������
}

/*----------------------------------------------*
 *				    д�ڲ�eeprom                *
 *----------------------------------------------*/
void inter_eeprom_write(unsigned char addr, unsigned char data) 
{
  	loop_until_bit_is_clear(EECR, EEWE);// �ȴ�д���
  	EEAR = addr;
  	EEDR = data;
  	cli();                				// �ر������ж�
  	EECR |= _BV(EEMWE);   				// �˲���������4��ʱ�������ڷ���
  	EECR |= _BV(EEWE);
  	sei();                				// ���ж�
}

int main (void)
{
	cpu_init();

	while(1)
	{
		asm("wdr");	
		if (tLap == 0xFFFF)
		{
      		cli();
     		/* �رյ���LED������������ */
      		set_led(0, bitFRONT);
      		set_led(0, bitBACK);
      		HALL_PORT &=~_BV(bitHALLPWR);
      		/* �رտ��Ź� */
      		WDTCSR |= _BV(WDCE) | _BV(WDE);
      		WDTCSR = 0;
      		/* ���ߴ��� */
     		MCUCR |= _BV(SM1) | _BV(SM0) | _BV(SE);
     		sei();
      		asm("sleep");
    	}
	}

}

/*----------------------------------END OF FILE-------------------------------*/

