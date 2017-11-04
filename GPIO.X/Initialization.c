#include <xc.h>
#include <pic16f1579.h>
#include "Define.h"


void initCLOCK(void)
{
    OSCCONbits.SPLLEN=1;//pll enabled
    OSCCONbits.IRCF=0b1110;// 8mhz hfintosc
    OSCCONbits.SCS=0b00;// internal oscillator block
}

void initPINS(void)
{
    /**
    LATx registers
    */   
    LATA = 0x00;    
    LATB = 0x00;    
    LATC = 0x00;    

    /**
    TRISx registers
    */    
    TRISA = 0x19;
    TRISB = 0xE0;
    TRISC = 0x08;

    /**
    ANSELx registers
    */   
    ANSELC = 0xC7;
    ANSELB = 0x30;
    ANSELA = 0x17;

    /**
    WPUx registers
    */ 
    WPUB = 0xF0;
    WPUA = 0x3F;
    WPUC = 0xFF;
    OPTION_REGbits.nWPUEN = 0;

    GIE = 0;
    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCKbits.PPSLOCKED = 0x00; // unlock PPS

    RXPPSbits.RXPPS = 0x13;   //RC3->EUSART:RX;
    INTPPSbits.INTPPS = 0x0E;   //RB6->EXT_INT:INT;
    RC4PPSbits.RC4PPS = 0x09;   //RC4->EUSART:TX;
    ADCACTPPSbits.ADCACTPPS = 0x0F;   //RB7->ADC1:ADCACT;
    RA5PPSbits.RA5PPS = 0x03;   //RA5->PWM1:PWM1OUT;

    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCKbits.PPSLOCKED = 0x01; // lock PPS

}
