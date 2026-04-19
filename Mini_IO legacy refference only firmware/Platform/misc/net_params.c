// net_params.h

#include <string.h>
#include <stdio.h>

#include "debug.h"
#include "net_params.h"
#include "flash.h"

_net_params net_params;


Md5 net_params_md5;
unsigned char params_digest[MD5_DIGEST_SIZE];
unsigned char params_digest2[MD5_DIGEST_SIZE];



//void net_params_init(void)
//{
//	int i;
//    i=FlashRead((unsigned char*)&net_params, FLASH_NET_PARAMS, sizeof(_net_params));
//    if (i!=sizeof(_net_params))
//    {
//    	debug_printf("FlashRead returned %d instead of %d\r\n", i, sizeof(_net_params));
//    	FlashRead((unsigned char*)&net_params, FLASH_NET_PARAMS, sizeof(_net_params));
//    }
//    if (net_params.MAGIC!=NET_PARAMS_MAGIC)
//    {
//        debug_printf("net params magic is incorrect, setting defaults\r\n");
//        set_default_net_params();
//        save_net_params();
//    }
//    else
//    {
//        // check hash
//        memcpy(params_digest, net_params.hash, MD5_DIGEST_SIZE);
//        memset(net_params.hash, 0, MD5_DIGEST_SIZE);
//        InitMd5(&net_params_md5);
//        Md5Update(&net_params_md5, (unsigned char*)&net_params, sizeof(_net_params));
//        Md5Final(&net_params_md5, params_digest2);
//
//        if (memcmp(params_digest2, params_digest, MD5_DIGEST_SIZE)!=0)
//        {
//        	debug_printf("Net Params digest is wrong, defaulting\r\n");
//        	debug_printf("SAVED HASH:      ");
//        	for(i=0; i<MD5_DIGEST_SIZE; i++) { debug_printf("%02X ", params_digest[i]); };
//        	debug_printf("\r\nCALCULATED HASH: ");
//        	for(i=0; i<MD5_DIGEST_SIZE; i++) { debug_printf("%02X ", params_digest2[i]); };
//        	debug_printf("\r\n");
//            set_default_net_params();
//            save_net_params();
//        }
//    }
//}

//void save_net_params(void)
//{
//    FlashEraseArea(FLASH_NET_PARAMS, sizeof(_net_params));
//
//    // Calculate hash
//    memset(net_params.hash, 0, MD5_DIGEST_SIZE); // clear out stored hash
//    InitMd5(&net_params_md5);
//    Md5Update(&net_params_md5, (unsigned char*)&net_params, sizeof(_net_params));
//    Md5Final(&net_params_md5, params_digest);
//    memcpy(net_params.hash, params_digest, MD5_DIGEST_SIZE); // set MD5 hash
//
//    FlashWrite(FLASH_NET_PARAMS, (unsigned char*)&net_params, sizeof(_net_params));
//}

//void set_default_net_params(void)
//{
//    memset(&net_params, 0, sizeof(_net_params));
//    net_params.MAGIC = NET_PARAMS_MAGIC;
//
//	net_params.net.dhcp=0;
//	net_params.net.ip[0]=192;
//	net_params.net.ip[1]=168;
//	net_params.net.ip[2]=1;
//	net_params.net.ip[3]=55;
//
//	net_params.net.nm[0]=255;
//	net_params.net.nm[1]=255;
//	net_params.net.nm[2]=255;
//	net_params.net.nm[3]=0;
//
//	net_params.net.gw[0]=192;
//	net_params.net.gw[1]=168;
//	net_params.net.gw[2]=1;
//	net_params.net.gw[3]=1;
//
//	net_params.net.dns[0]=8;
//	net_params.net.dns[1]=8;
//	net_params.net.dns[2]=8;
//	net_params.net.dns[3]=8;
//
//	net_params.sntp.enable=1;
//	net_params.sntp.port=123;
//	sprintf((char*)net_params.sntp.server, "pool.ntp.org");
//
//	sprintf(net_params.smtp.host_id, "BTI-Emailer");
//	net_params.smtp.port = 25;
//
//	net_params.snmp.manager_port = 161;
//	sprintf(net_params.snmp.read_community, "public");
//	sprintf(net_params.snmp.write_community, "private");
//
//
//}


/***   End Of File   ***/
