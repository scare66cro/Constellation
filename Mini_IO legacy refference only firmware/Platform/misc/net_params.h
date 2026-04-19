#ifndef NET_PARAMS_H
#define NET_PARAMS_H

#include "md5.h"
#include "Servers/smtp_blocking.h"

void net_params_init(void);
void set_default_net_params(void);
void save_net_params(void);

#define NET_PARAMS_MAGIC 0xBF49800C

/************** SMNP PARAMS *****************/
#define SNMP_COMMUNITY_LEN	30

typedef struct
{
	unsigned char enabled;
	unsigned char manager_ip[4];
	unsigned short manager_port;
	char read_community[SNMP_COMMUNITY_LEN];
	char write_community[SNMP_COMMUNITY_LEN];
}_snmp_params;



/************** GENERAL NET PARAMS *****************/

typedef struct
{
	unsigned char dhcp;
	unsigned char ip[4];
	unsigned char nm[4];
	unsigned char gw[4];
	unsigned char dns[4];
}_net_device_conf;

/************* SNTP Params ***********************/
#define SNTP_SERVER_LEN	30

typedef struct
{
	unsigned char enable;
	char          server[SNTP_SERVER_LEN];
	short         port;
}_sntp_params;


/************** GENERAL PARAMS STRUCT **************/

typedef struct
{
    unsigned int MAGIC;

	_net_device_conf 	net;
	_sntp_params		sntp;
	_smtp_params        smtp;
	_snmp_params		snmp;

	unsigned char hash[MD5_DIGEST_SIZE];
}_net_params;

extern _net_params net_params;


#endif
/***   End Of File   ***/
