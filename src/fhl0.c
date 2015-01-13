/******************************************************************************
 * fhl.c - ±àÒë»·¾³AVRGCC
 * 
 * Copyright 1998-2003 Routon Technology Co.,Ltd.
 * 
 * DESCRIPTION: - 
 *    ·ç»ğÂÖ¿ØÖÆÔ´Âë
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
 *	  ¶Ë¿Ú¶¨Òå    *
 *----------------*/

#define BOTTON_PIN 				PIND
#define bitBOTTON 				2
#define HALL_PORT 				PORTD
#define HALL_PIN 				PIND
#define bitHALLOUT 				3
#define bitHALLPWR  			6

/*----------------*
 *	 74HC595Òı½Å  *
 *----------------*/
#define SEL_SIDE 				PORTD				// Ëø´æÊ±ÖÓ--74HC595 12# 
#define bitFRONT 				4
#define bitBACK 				5
#define SET_SER 				PORTB|= _BV(PB4)	// Êı¾İ------74HC595 14# 
#define CLR_SER 				PORTB&=~_BV(PB4)	
#define SET_SCLK 				PORTB|= _BV(PB5)	// ÒÆÎ»Ê±ÖÓ--74HC595 11# 
#define CLR_SCLK 				PORTB&=~_BV(PB5)

/*----------------*
 *	   ºê¶¨Òå     *
 *----------------*/
#define EN_ANIMATE 				1					// Ö§³Ö¶¯»­

#define NUM_PIXELS 				256 				// ÏñËØÊı256 (¸ÕºÃÒ»¸ö×Ö½Ú)
#define NUM_LEDS 				32 					// µ¥ÃæLEDÊı¾İ

#define HALL_DEBOUNCE 			4  					// 
#define BUTTON_DEBOUNCE  		100					// 100ms

#define STANDBY_TIMEOUT 		5*((F_CPU/256)/0xff)// F_CPU¾§ÕñÆµÂÊ Ô¼5S
#define POWEROFF_TIMEOUT 		2*60*((F_CPU/256)/0xff) 

/*----------------*
 *	  EEµØÖ·¶¨Òå  *
 *----------------*/
#ifndef EEWE
#define EEWE 					1
#endif
#ifndef EEMWE
#define EEMWE 					2
#endif

#define E2_ROTATION_OFFSET 		0x00
#define E2_MIRROR 				0x01				// ¾µÏà
#define E2_ANIMATION 			0x02				// ¶¯»­

/*----------------*
 *	  ±äÁ¿¶¨Òå    *
 *----------------*/
volatile unsigned int  tLap; 						// ÂÖ×Ó×ªÒ»È¦ËùÓÃÊ±¼ä´æ·Å±äÁ¿ 
volatile unsigned char tHall;						// »ô¶û´«¸ĞÆ÷¼ì²âÊ±¼ä

unsigned char mirror;								// ¾µÏà
unsigned char fleds[4], bleds[4];  					// Ç°¡¢ºóÃæLED¶ÔÓ¦»º´

unsigned char tanim_dalay    = 6;					// ¶¯»­±£³Ö
volatile unsigned int tanim  = 0;  					// Ê±¼ä¿ØÖÆ±äÁ¿

volatile unsigned int anim_e2_offset= 0;
volatile unsigned int curr_e2_addr  = 0;			// 

/*----------------*
 *	  Í¼ÏñÊı¾İ    *
 *----------------*/
unsigned char val[] EEMEM ={						// ´æÈëE2 EEMEM µÈÍ¬ÓÚ __attribute__((section(".eeprom"))) 
	"string in eeprom"};

/*----------------*
 *	  º¯ÊıÉùÃû    *
 *----------------*/
 
void cpu_init	 (void);
void refresh_leds(void);
void ctrl595_out (unsigned char);
void set_led	 (unsigned char , unsigned char );  
void inter_eeprom_write	(unsigned char , unsigned char );
unsigned char inter_eeprom_read	(unsigned char ); 
unsigned char spi_transfer		(unsigned char ); 


/*----------------------------------------------*
 *					CPU³õÊ¼»¯                   *
 *----------------------------------------------*/
void cpu_init(void) 
{
	/* ¶Ë¿ÚÅäÖÃ */
 	PORTB = 0xC7;
 	DDRB  = 0x38;
 	PORTC = 0x7F; //m103 output only
 	DDRC  = 0x00;
 	PORTD = 0x8F;
 	DDRD  = 0x72;

  	PORTD = (_BV(bitBOTTON) | _BV(bitHALLOUT) | _BV(bitHALLPWR))  
    & ~_BV(bitFRONT) & ~_BV(bitBACK);  	
    
	/* ¿´ÃÅ¹· */
	MCUSR  = 0;			// MCUSR ÖĞµÄWDRFÇåÁã
  	WDTCSR = _BV(WDE) | _BV(WDP2) | _BV(WDP1); // Ê¹ÄÜ 1 S
  	
  	/* Íâ²¿ÖĞ¶Ï0¡¢1µÍµçÆ½´¥·¢£¬0:°´¼ü£»1:´«¸ĞÆ÷ */
  	EICRA  = _BV(ISC11) & ~_BV(ISC10) & _BV(ISC01) &  ~_BV(ISC00);

  	/* ´ò¿ªÍâ²¿ÖĞ¶Ï */
  	EIMSK  = _BV(INT1) | _BV(INT0);
  	
	/* ¶¨Ê±Æ÷0 */
  	TCCR0A = 0;				
  	TCNT0  = 0xff;			// 100us
  	TCCR0B = _BV(CS02); 	// 256·ÖÆµ clk/256 
  	TIMSK0|= _BV(TOIE0); 	// ´ò¿ªÒç³öÖĞ¶Ï
  
  	/* ¶¨Ê±Æ÷1 CTCÄ£Ê½ */
  	TCCR1A = 0;
  	TCCR1B = _BV(WGM12); 	// WGM: 4) CTC, TOP=OCRnA

  	tHall = 0;
  	tLap  = 0;
}

/*----------------------------------------------*
 *	      Íâ²¿ÖĞ¶Ï0:°´¼ü¼ì²â(µÍµçÆ½)            *
 *----------------------------------------------*/
SIGNAL (INT0_vect) 
{
  	unsigned int t=0;

  	while (!(BOTTON_PIN & _BV(bitBOTTON)))	// µÈ´ıÊÍ·Å°´¼ü 
	{
    	t++;
    	_delay_ms(1);
  	}
  	if (t > BUTTON_DEBOUNCE) 
	{
    	if (t < 500UL) 
		{
      		/* ¶Ì°´¼ü£¬Ğ¡ÓÚ500ms ´ò¿ª¿´ÃÅ¹·ËÀÑ­»·Ôì³É¸´Î»£¬¼¤»îÏµÍ³ */
      		WDTCSR = _BV(WDE);	// Ê¹ÄÜ¿´ÃÅ¹·
      		while (1);
   		} 
		else	// ³¤°´¼üĞİÃß´ı»ú
      		tLap = 0xffff;
  	}
}

/*----------------------------------------------*
 *	     Íâ²¿ÖĞ¶Ï1:»ô¶û´«¸ĞÆ÷´¥·¢(µÍµçÆ½)       *
 *----------------------------------------------*/
SIGNAL (INT1_vect) 
{
  	if (tHall > HALL_DEBOUNCE) 				// Èí¿¹É§ÈÅ£¬·ÀÖ¹Îó´¥·¢
	{
#if EN_ANIMATE								// ¶¯»­Ö§³Ö
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
		/* ÎÒÃÇÖªµÀ×îºóÒ»´Î»ô¶û´«¸ĞÆ÷´¥·¢ºóµÄºÁÃëÊı£¬
		ÒÔ¼°Ã¿´ÎÉ¨ÃèÓĞ256ÌõÉäÏß×´¡°ÏñËØ¡± ÒÔ±ã·Ö±ğÔÚ
		Ã¿¸öÏñËØ¼ä¶ÏÆÚ¼äÈ¡µÃ±ØÒªµÄºÁÃëÊı£¬ÏÖÔÚ½ö½öÊ¹
		µÃ¶¨Ê±Æ÷1´¦ÓÚÄÇ¸ö±ÈÂÊ£¬Ò²¾ÍÊÇT/C1ÖĞ¶ÏÊ±¼äÊÇ
		T0µÄ256Ö®1£¬T0ËùÑ¡Ê±ÖÓ256·ÖÆµ£¬ÔòT/C1ËùÑ¡Ê±
		Ê±ÖÓ²»·ÖÆµ*/

    	TCNT1 = 0;								// timer1¼ÆÊıÖµÇå0
    	if ((tLap < 0xff) && (tLap > 0x3)) 		// Ê±¼äÔÚ¶ÌÌ«³¤¶¼²»´¦Àí
		{
      		OCR1A = (tLap << 8) | TCNT0;		// ÊäÁË±È½Ï¼Ä´æÆ÷¸³Öµ
      		TCNT0 = 0;							// T0¼ÆÊıÖµÇåÁã
      		
      		/* È¡³öÔ´Êı¾İ´æ·ÅµØÖ· */
      		curr_e2_addr = (inter_eeprom_read(E2_ROTATION_OFFSET) % NUM_PIXELS)* 4;
      		mirror = inter_eeprom_read(E2_MIRROR);
      		
			TCCR1B |= _BV(CS10);                // Ê±ÖÓÎŞ·ÖÆµ
      		TIMSK1 |= _BV(OCIE1A);				// T/C1Êä³ö±È½ÏA²úÉúÖĞ¶Ï
   	 	} 
		else 
		{
      		set_led(2, bitFRONT);
      		set_led(2, bitBACK);
      		TCCR1B &= ~_BV(CS10);				// ÎŞÊ±ÖÓÔ´£¬Í£Ö¹T/C1
    	}   
    	tLap = 0;
  	}
  	tHall = 0;
}

/*----------------------------------------------*
 *			   T0 100u¶¨Ê±ÖĞ¶Ï                  *
 *----------------------------------------------*/
SIGNAL (TIMER0_OVF_vect) 
{
  	if (tHall != 0xff)
    	tHall++;
  
  	if (tLap != 0xffff)
    	tLap++;
}

/*----------------------------------------------*
 *		T/C1 CTC(±È½ÏÆ¥ÅäÊ±ÇåÁã¶¨Ê±Æ÷)Ä£Ê½      *
 *     ------------------------------------     *
 * ÊıÆ÷ÊıÖµTCNT0Ò»Ö±ÀÛ¼Óµ½TCNT0ÓëOCR0AÆ¥Åä,È»ºó * 
 * TCNT0 ÇåÁã.²úÉú´ËÖĞ¶Ï                        *
 *----------------------------------------------*/
SIGNAL (TIMER1_COMPA_vect) 
{
  	unsigned int addr;

  	sei(); 

  	addr = curr_e2_addr;			// Ô´Êı¾İµØÖ·

  	if (tLap < STANDBY_TIMEOUT) 	// ×ªÒ»È¦µØÊ±¼äĞ¡ÓÚ5S²ÅÏÔÊ¾
  	{    
    	addr %= (NUM_PIXELS * 4);
    
    	ctrl595_out(inter_eeprom_read(addr + anim_e2_offset));// ÒªÏÔÊ¾Êı¾İËÍ³ö

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
    	/* ¹ØÖĞ¶Ï£¬È·±£ÔÚÍ¬Ò»Ê±¼äÆäËüÖĞ¶Ï´ò¶ÏĞ´Èë´ËµØÖ· */
    	cli();
    	if (addr == (curr_e2_addr%(NUM_PIXELS*4))) 
    	{
      		curr_e2_addr = addr+4;	// µØÖ·Ö¸ÕëÒÆÏòÏÂÒ»Êı¾İÇø
      	}
      	//curr_e2_addr = addr;
    	sei();
  	} 
  	else 	// ×ªÒ»È¦³¬¹ı5S
  	{
    	cli();
    	TCCR1B &= ~0x7;				// ¹Ø±ÕT/C1
    	sei();
    	set_led(2, bitFRONT);		// turn off all but one LED;
    	set_led(2, bitBACK);
  	}
}
/*----------------------------------------------*
 *				  595Êä³ö¿ØÖÆ                   *
 *----------------------------------------------*/
 
void ctrl595_out(unsigned char sel) 
{ 
	unsigned int i; 
	unsigned char *leds;
	unsigned long dat;

  	if (sel == bitFRONT)
    	leds = fleds;			// Ç°Ãæ
  	else
    	leds = bleds;   		// ºóÃæ

	dat = ((long)leds[3]<<24)|((long)leds[2]<<16)|((long)leds[1]<<8)|leds[0];

	for (i=0;i<NUM_LEDS;i++) 
   	{  		
		CLR_SCLK;				// ÒÆÎ»Ê±ÖÓÖÃµÍ 

    	if (dat&1) 
			SET_SER;			// ÖÃ¸ßÊı¾İÊ±ÖÓ
		else 
			CLR_SER;  			// ½ÓµÍÊı¾İÊ±ÖÓ		
    	dat>>=1; 

    	SET_SCLK;				// __¡ü   ÒÆÎ»Ê±ÖÓÉÏÉıÑØ,Êı¾İ½øÈëÒÆÎ»¼Ä´æÆ÷   
    } 

	SEL_SIDE |= _BV(sel);
  	asm("nop"); asm("nop"); 
  	asm("nop"); asm("nop");		//    __
  	SEL_SIDE &=~_BV(sel);		// __¡ü   Ëø´æµçÆ½ÉÏÉıÑØ£¬Êı¾İÊä³öµ½²¢ĞĞ¶Ë¿Ú  
} 

/*----------------------------------------------*
 *				    LEDS ¿ØÖÆ                   *
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
 *				    LEDS Ë¢ĞÂ                   *
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
 *				    ¶ÁÄÚ²¿eeprom                *
 *----------------------------------------------*/
unsigned char inter_eeprom_read(unsigned char addr) 
{
 	loop_until_bit_is_clear(EECR, EEWE);// µÈ´ıĞ´Íê³É
  	EEAR = addr;
  	EECR |= _BV(EERE);        			// ¿ªÊ¼¶ÁEEPROM
  	return EEDR;            			// ·µ»Ø½ö1¸öÊ±ÖÓÖÜÆÚ
}

/*----------------------------------------------*
 *				    Ğ´ÄÚ²¿eeprom                *
 *----------------------------------------------*/
void inter_eeprom_write(unsigned char addr, unsigned char data) 
{
  	loop_until_bit_is_clear(EECR, EEWE);// µÈ´ıĞ´Íê³É
  	EEAR = addr;
  	EEDR = data;
  	cli();                				// ¹Ø±ÕËùÓĞÖĞ¶Ï
  	EECR |= _BV(EEMWE);   				// ´Ë²Ù×÷±ØĞëÔÚ4¸öÊ±ÖÓÖÜÆÚÄÚ·¢Éú
  	EECR |= _BV(EEWE);
  	sei();                				// ´ò¿ªÖĞ¶Ï
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
     		/* ¹Ø±ÕµÄÓĞLED¼°»ô¶û´«¸ĞÆ÷ */
      		set_led(0, bitFRONT);
      		set_led(0, bitBACK);
      		HALL_PORT &=~_BV(bitHALLPWR);
      		/* ¹Ø±Õ¿´ÃÅ¹· */
      		WDTCSR |= _BV(WDCE) | _BV(WDE);
      		WDTCSR = 0;
      		/* ĞİÃß´ı»ú */
     		MCUCR |= _BV(SM1) | _BV(SM0) | _BV(SE);
     		sei();
      		asm("sleep");
    	}
	}

}

/*----------------------------------END OF FILE-------------------------------*/

