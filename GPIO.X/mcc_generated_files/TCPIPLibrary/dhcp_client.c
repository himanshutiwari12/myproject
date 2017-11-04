/**
  DHCP v4 client implementation
	
  Company:
    Microchip Technology Inc.

  File Name:
    dhcp_client.c

  Summary:
     This is the implementation of DHCP client.

  Description:
    This source file provides the implementation of the DHCP client protocol.

 */

/*

©  [2015] Microchip Technology Inc. and its subsidiaries.  You may use this
software and any derivatives exclusively with Microchip products. 
  
THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES, WHETHER EXPRESS, 
IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES
OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE, OR
ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION WITH ANY OTHER PRODUCTS, OR
USE IN ANY APPLICATION. 

IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER
RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF
THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT ALLOWED
BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY RELATED TO THIS
SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY
TO MICROCHIP FOR THIS SOFTWARE.

MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE
TERMS. 

*/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>

#include "ethernet_driver.h"
#include "mac_address.h"
#include "network.h"
#include "udpv4.h"
#include "udpv4_port_handler_table.h"
#include "ipv4.h"
#include "arpv4.h"
#include "dhcp_client.h"
#include "ip_database.h"
#include "lfsr.h"

#define DHCP_HEADER_SIZE 240

#if ( DHCP_PACKET_SIZE & 1)
#define ZERO_PAD_DHCP
#define DHCP_REQUEST_LENGTH (DHCP_PACKET_SIZE + 1)
#else
#undef ZERO_PAD_DHCP
#define DHCP_REQUEST_LENGTH DHCP_PACKET_SIZE
#endif


/**
  Section: Enumeration Definition
*/
typedef enum
{
    DHCP_DISCOVER = 1,
    DHCP_OFFER, DHCP_REQUEST, DHCP_DECLINE, DHCP_ACK, DHCP_NACK, DHCP_RELEASE,
    DHCP_INFORM, DHCP_FORCERENEW, DHCP_LEASEQUERY, DHCP_LEASEUNASSIGNED, DHCP_LEASEUNKNOWN,
    DHCP_LEASEACTIVE, DHCP_BULKLEASEQUERY, DHCP_LEASEQUERYDONE
}dhcp_type;

typedef enum
{
    SELECTING, REQUESTING, RENEWLEASE, BOUND
}dhcp_rx_client_state;
typedef enum
{
    INIT_TIMER, WAITFORTIMER, STARTDISCOVER, STARTREQUEST
}dhcp_timer_client_state;


typedef struct
{
    // option data
    uint32_t dhcpIPAddress;
    uint32_t subnetMask;
    uint32_t routerAddress;
    uint32_t dnsAddress[2]; // only capture 2 DNS addresses
    uint32_t ntpAddress[2];
    uint32_t gatewayAddress;
    uint32_t xidValue;
    uint32_t t1; // name per the RFC page 35
    uint32_t t2; // name per the specification page 35
    uint32_t leasee_ip;
} dhcp_data_t;

typedef struct
{
    // state data
    dhcp_rx_client_state rxClientState;
    dhcp_timer_client_state tmrClientState;
} dhcp_state_t;

dhcp_data_t dhcpData = {0,0,0,0,0,0,0,0,0,10,3600,0};
dhcp_state_t dhcpState = {SELECTING,INIT_TIMER};

bool sendRequest(dhcp_type type);

bool sendDHCPDISCOVER(void)
{
    dhcpData.dhcpIPAddress = ipdb_getAddress();
    dhcpData.xidValue = lfsr();
    dhcpData.xidValue <<= 8;
    dhcpData.xidValue |= lfsr();
    dhcpData.xidValue <<= 8;
    dhcpData.xidValue |= lfsr();
    dhcpData.xidValue <<= 8;
    dhcpData.xidValue |= lfsr();
    return sendRequest(DHCP_DISCOVER);
}

bool sendDHCPREQUEST(void)
{
    return sendRequest(DHCP_REQUEST);
}

bool sendDHCPDECLINE(void)
{
    return sendRequest(DHCP_DECLINE);
}

bool sendRequest(dhcp_type type)
{
    // creating a DHCP request
    error_msg started;


    started = UDP_Start(0xFFFFFFFF,68,67);
    if(started==SUCCESS)
    {
        UDP_Write32(0x01010600);        // OP, HTYPE, HLEN, HOPS
        UDP_Write32(dhcpData.xidValue);          // XID : made up number...
        UDP_Write32(0x00008000);        // SECS, FLAGS (broadcast)
        if(type == DHCP_REQUEST)
        {
            UDP_Write32(0);
        }
        else
        {
            UDP_Write32(dhcpData.dhcpIPAddress);   // CIADDR
        }
        UDP_Write32(0);                 // YIADDR
        UDP_Write32(0);                 // SIADDR
        UDP_Write32(0);                 // GIADDR
        UDP_WriteBlock((char *)&hostMacAddress,6); // Hardware Address
        DHCP_WriteZeros(202);           // 0 padding  + 192 bytes of BOOTP padding
        UDP_Write32(0x63825363);        // MAGIC COOKIE - Options to Follow
        // send the options
        // option 12 - DHCP Name
        UDP_Write8(12); UDP_Write8(strlen(dhcpName)); UDP_WriteString(dhcpName);
        // option 42 - NTP server name
        UDP_Write8(42); UDP_Write8(4); UDP_Write32(0);
        // option 53 - Request type
        UDP_Write8(53); UDP_Write8(1); UDP_Write8(type);
        if(type == DHCP_REQUEST)
        {
            // option 50 - requested IP address... DO this if we have a valid one to re-request
            UDP_Write8(50);UDP_Write8(4);UDP_Write32(dhcpData.dhcpIPAddress);
            // option 54 - 
            UDP_Write8(54);UDP_Write8(4);UDP_Write32(dhcpData.leasee_ip);
        }
        // option 55
        UDP_Write8(55); UDP_Write8(4); UDP_Write8(1); UDP_Write8(3); UDP_Write8(6); UDP_Write8(15);
        // option 57 - Maximum DHCP Packet
        UDP_Write8(57); UDP_Write8(2); UDP_Write16(512);
        // send option 61 (MAC address)
        UDP_Write8(61); UDP_Write8(7); UDP_Write8(1); UDP_WriteBlock(&hostMacAddress,6);
        UDP_Write8(255); // finished
        
    #ifdef ZERO_PAD_DHCP
        UDP_Write8(0);   // add a byte of padding to make the total length even
    #endif
        UDP_Send();
        return true;
    }
    return false;
}

void DHCP_init(void)
{
}

void DHCP_Manage(void)
{
    static time_t managementTimer = 0;    
    time_t now;
    now = time(0);
    
    if(managementTimer <= now)
    {
        switch(dhcpState.tmrClientState)
        {
            case INIT_TIMER:
                dhcpData.t1 = 4;
                dhcpState.tmrClientState = WAITFORTIMER;
                break;
            case WAITFORTIMER:
                if(dhcpData.t1 == 2)
                {
                    dhcpState.tmrClientState = STARTDISCOVER;
                }
                else dhcpData.t1 --;
                if(dhcpData.t2 == 2)
                {
                    dhcpState.tmrClientState = STARTREQUEST;
                }
                else dhcpData.t2 --;
                break;
            case STARTDISCOVER:
                if(sendDHCPDISCOVER())
                {
                    dhcpData.t1 = 10; // retry in 10 seconds
                    dhcpData.t2 = LONG_MAX;
                    dhcpState.rxClientState = SELECTING;
                    dhcpState.tmrClientState = WAITFORTIMER;                    
                }
                break;
            case STARTREQUEST:
                if(sendDHCPREQUEST())
                {
                    dhcpData.t1 = 30;
                    dhcpData.t2 = 15;
                    if(dhcpState.rxClientState == BOUND )dhcpState.rxClientState = RENEWLEASE;
                    else dhcpState.rxClientState = REQUESTING;
                    dhcpState.tmrClientState = WAITFORTIMER;
                }
                break;
            default:
                dhcpState.tmrClientState = INIT_TIMER;
                break;
        }
    }
    managementTimer = now + 1;
}

void DHCP_Handler(int length)
{
    // this function gets called by the UDP port handler for port 67
    uint8_t chaddr[16];
    uint8_t messageType;
    
    bool acceptOffers;
    bool declineOffers;
    bool acceptNACK;
    bool acceptACK;

    uint32_t siaddr;

    
    
    switch(dhcpState.rxClientState)
    {
        default:
        case SELECTING: // we can accept offers... drop the rest.            
            acceptOffers    = true;
            declineOffers   = false;
            acceptNACK      = false;
            acceptACK       = false;
            break;
        case REQUESTING: // we can accept an ACK or NACK... decline offers and drop the rest           
            acceptOffers    = false;
            declineOffers   = true; //db
            acceptNACK      = true;
            acceptACK       = true;
            break;
        case RENEWLEASE:           
            acceptOffers    = false;
            acceptNACK      = true;//a bound lease renewal can have a Nack back
            acceptACK       = true;//a bound lease renewal can have an ack back
            break;            
        case BOUND:     // drop everything... the timer will pull us out of here.            
            acceptOffers    = false;
            acceptNACK      = false;
            acceptACK       = false;
            break;
    }
    
    if(acceptOffers || declineOffers || acceptNACK || acceptACK)
    {
        dhcp_data_t localData = {0,0,0,0,0,0,0,0,0,3600,3600,0};
        localData.xidValue = dhcpData.xidValue;
        
        if(length > DHCP_HEADER_SIZE)
        {           
            if(0x0201 == UDP_Read16())
            {
                if(0x06 == UDP_Read8()) //HLEN - is set to 6 because an Ethernet address is 6 Octets long
                {
                    UDP_Read8(); // HOPS
                    if(dhcpData.xidValue == UDP_Read32()) // xid check
                    {
                        UDP_Read16(); // SECS
                        UDP_Read16(); // FLAGS
                        UDP_Read32(); // CIADDR
                        localData.dhcpIPAddress = UDP_Read32(); // YIADDR
                            if((localData.dhcpIPAddress != SPECIAL_IPV4_BROADCAST_ADDRESS) && (localData.dhcpIPAddress != LOCAL_HOST_ADDRESS))
                            {
                                siaddr = UDP_Read32(); // SIADDR
                                if((siaddr != SPECIAL_IPV4_BROADCAST_ADDRESS) && (siaddr != LOCAL_HOST_ADDRESS))
                                {
                        UDP_Read32(); // GIADDR
                        UDP_ReadBlock(chaddr, sizeof(chaddr)); // read chaddr
                        if(memcmp(chaddr, &hostMacAddress.s, 6)== 0 || memcmp(chaddr, &broadcastMAC.s, 6)== 0 || (strlen(chaddr)== 0)) // only compare 6 bytes of MAC address.
                        {
                            ETH_Dump(64); // drop SNAME
                            ETH_Dump(128); // drop the filename
                            if(UDP_Read32() == 0x63825363)
                            {
                                length -= DHCP_HEADER_SIZE;
                                while(length>0)
                                {
                                    // options are here!!!
                                    uint8_t option, optionLength;
                                    option = UDP_Read8();
                                    optionLength = UDP_Read8();
                                    length -= 2 + optionLength;
                                    switch(option)
                                    {
                                        case 1: // subnet mask                                            
                                            localData.subnetMask = UDP_Read32();
                                            break;
                                        case 3: // router
                                            localData.routerAddress = UDP_Read32();
                                            break;
                                        case 6: // DNS List
                                            {
                                                uint8_t count=0;
                                                while(optionLength >= 4)
                                                {
                                                    uint32_t a = UDP_Read32();
                                                    if(count < 2)
                                                        localData.dnsAddress[count++] = a;

                                                    optionLength -= 4;
                                                }
                                            }
                                            break;
                                        case 42: // NTP server
                                            {
                                                uint8_t count=0;
                                                while(optionLength >= 4)
                                                {
                                                    uint32_t a = UDP_Read32();
                                                    if(count < 2)
                                                        localData.ntpAddress[count++] = a;

                                                    optionLength -= 4;
                                                }
                                            }
                                            break;
                                        case 51: // lease time
                                            if(optionLength >= 4)
                                            {
                                                localData.t1 = UDP_Read32();
                                                localData.t2 = localData.t1 >> 1; //be default set to 1/2 the lease length
                                                optionLength -= 4;
                                            }
                                            break;
                                        case 54: // DHCP server
                                            localData.leasee_ip = UDP_Read32();   
                                            optionLength -= 4;
                                            break;
                                        case 53:
                                            messageType=UDP_Read8();
                                            optionLength -=1;
                                            break;
                                        default:
                                            ETH_Dump(optionLength); // dump any unused bytes
                                            break;
                                    } // option switch
                                } // length loop
                            } // magic number test
                            else
                            {
                                //DHCP fail Magic Number check
                            }
                        } // mac address test
                        else
                        {
                            //DHCP fail MAC address check
                        }
                                }//SIADDR test
                                else
                                {
                                    //DHCP fail SIADDR check
                                }
                            }//YIADDR test
                            else
                            {
                                //DHCP fail YIADDR check
                            }
                    } // xid test
                    else
                    {                   
                        //DHCP fail XID check
                    }
                } //HLEN test
                else
                {
                    //DHCP fail Hardware Length
                }
            } // 201 test
            else
            {
            //DHCP fail 201 check
            }
        } // short packet test
        else
        {
            //DHCP fail short packet check
        }
        switch(messageType)
        {
            case DHCP_OFFER:
                if(acceptOffers)
                {
                    dhcpData = localData; // capture the data in the offer
                    sendDHCPREQUEST();
                    dhcpState.rxClientState = REQUESTING;
                }
                if(declineOffers)
                {
                    sendDHCPDECLINE();
                }
                break;
            case DHCP_ACK:
                if(acceptACK)
                {
                    dhcpData.t1 = localData.t1;
                    dhcpData.t2 = localData.t2;
                    ipdb_setAddress(dhcpData.dhcpIPAddress);
                    ipdb_setDNS(0,dhcpData.dnsAddress[0]);
                    ipdb_setDNS(1,dhcpData.dnsAddress[1]);
                    ipdb_setRouter(dhcpData.routerAddress);
                    ipdb_setGateway(dhcpData.gatewayAddress);
                    ipdb_setSubNetMASK(dhcpData.subnetMask);
                    if(dhcpData.ntpAddress[0])
                    {
                        ipdb_setNTP(0,dhcpData.ntpAddress[0]);
                        if(dhcpData.ntpAddress[1])
                            ipdb_setNTP(1,dhcpData.ntpAddress[1]);
                    }
                    dhcpState.rxClientState = BOUND;
                }
                break;
            case DHCP_NACK:
                if(acceptNACK)
                {
                    dhcpData.t1 = 0;
                    dhcpData.t2 = 0;
                    dhcpState.rxClientState = SELECTING;
                }
                break;
            default:
                break;
        }
    } // accept types test
    else
    {
        //DHCP fail accept types  check
    }
}

void DHCP_WriteZeros(uint16_t length)
{
    while(length--)
    {
        UDP_Write8(0);
    }
}