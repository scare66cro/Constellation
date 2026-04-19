// amazon_sdb.c

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "system.h"
#include "hmac.h"
#include "coding.h"
#include "sntp.h"
#include "lwip/tcp.h"
#include "amazon_sdb.h"

#define CREATE_DOMAIN 		0
#define DELETE_DOMAIN 		1
#define LIST_DOMAIN			2
#define DOMAIN_META			3
#define DELETE_ITEM			4
#define ADD_ATTRIBUTE		5
#define REPLACE_ATTRIBUTE	6
#define DELETE_ATTRIBUTE	7
#define GET_ALL_ATTRIBUTES	8
#define QUERY_ATTRIBUTES	9
#define QUERY_SELECT		10

char amazon_url[]="sdb.us-west-1.amazonaws.com";
char secret_key[]="REDACTED_SECRET_KEY";
char access_key[]="REDACTED_ACCESS_KEY";

static Hmac sdb_hmac;
static unsigned char sdb_digest[32];
static unsigned char sdb_signature[64];
static unsigned char sdb_params[2000];
static unsigned char request[2000];
static unsigned char sdb_timestamp[20];
static completed_cb callback_fn=NULL;
static struct tcp_pcb *request_pcb=NULL;

static void escape_signature(unsigned char *in, unsigned char *out);
static int calculate_signature(unsigned char *data_in, unsigned char *signature);
static unsigned char *print_timestamp(void);
static int generate_request(unsigned char *params, unsigned char *signature);
static int send_request(unsigned char *str);
static void sdb_close(struct tcp_pcb *pcb);

int list_domain(int max_domains, completed_cb cb)
{
	unsigned char *p1;
	
	callback_fn=cb;
	
	// calculate params
	sdb_params[0]='\0';
	sprintf((char*)sdb_params+strlen((char*)sdb_params), "GET\n%s\n/\n", amazon_url);
	p1=(sdb_params+strlen((char*)sdb_params)); // save the actual params part
	sprintf((char*)sdb_params+strlen((char*)sdb_params), "AWSAccessKeyId=%s&Action=ListDomains&MaxNumberOfDomains=%d&SignatureMethod=HmacSHA1&SignatureVersion=2&Timestamp=%s&Version=2009-04-15", access_key, max_domains, print_timestamp());
	
	calculate_signature(sdb_params, sdb_signature); // Calculate signature
	return generate_request(p1, sdb_signature);		// Generate and send the request
}

int create_domain(char *name, completed_cb cb)
{
	unsigned char *p1;
	
	callback_fn=cb;
	
	// calculate params
	sdb_params[0]='\0';
	sprintf((char*)sdb_params+strlen((char*)sdb_params), "GET\n%s\n/\n", amazon_url);
	p1=(sdb_params+strlen((char*)sdb_params)); // save the actual params part
	sprintf((char*)sdb_params+strlen((char*)sdb_params), "AWSAccessKeyId=%s&Action=CreateDomain&DomainName=%s&SignatureMethod=HmacSHA1&SignatureVersion=2&Timestamp=%s&Version=2009-04-15", access_key, name, print_timestamp());
	
	calculate_signature(sdb_params, sdb_signature); // Calculate signature
	return generate_request(p1, sdb_signature);		// Generate and send the request
}

int put_attribute(char *domain_name, char *item_name, char attribute_name[][MAX_ATTRIBUTE_LEN], char attribute_value[][MAX_ATTRIBUTE_LEN], int num_attributes, completed_cb cb)
{
	int i;
	unsigned char *p1;
	
	if (num_attributes>9) return -40; // the order of the items changes based on the number of them, stick to 9
	
	callback_fn=cb;
	
	// calculate params
	sdb_params[0]='\0';
	sprintf((char*)sdb_params+strlen((char*)sdb_params), "GET\n%s\n/\n", amazon_url);
	p1=(sdb_params+strlen((char*)sdb_params)); // save the actual params part
	sprintf((char*)sdb_params+strlen((char*)sdb_params), "AWSAccessKeyId=%s&Action=PutAttributes", access_key);
	
	for(i=0; i<num_attributes; i++)
	{
		sprintf((char*)sdb_params+strlen((char*)sdb_params), "&Attribute.%d.Name=%s&Attribute.%d.Value=%s", i+1, attribute_name[i], i+1, attribute_value[i]);
	}
	
	sprintf((char*)sdb_params+strlen((char*)sdb_params), "&DomainName=%s&ItemName=%s&SignatureMethod=HmacSHA1&SignatureVersion=2&Timestamp=%s&Version=2009-04-15", domain_name, item_name, print_timestamp());
	
	//debug_printf("Would have sent: %s\n", sdb_params);
	calculate_signature(sdb_params, sdb_signature); // Calculate signature
	i = generate_request(p1, sdb_signature);		// Generate and send the request
	if ((i!=0)&&(callback_fn!=NULL)){callback_fn(i);}
	return i;
}
	
void cancel_request(void)
{
	sdb_close(request_pcb);
}	

// PutAttribute: AWSAccessKeyId=REDACTED_ACCESS_KEY &Action=PutAttributes &Attribute.1.Name=Name1 &Attribute.1.Value=Value1 &DomainName=TESTDataLog-KEN&ItemName=ItemName-Date&SignatureMethod=HmacSHA1&SignatureVersion=2&Timestamp=2011-06-29T17%3A40%3A03.000Z&Version=2009-04-15'
// PutAttribute: AWSAccessKeyId=REDACTED_ACCESS_KEY &Action=PutAttributes &Attribute.1.Name=Name1-1 &Attribute.1.Value=Value1-1 &Attribute.2.Name=Name2-1&Attribute.2.Value=Value1-1&DomainName=TESTDataLog-KEN&ItemName=ItemName-Date&SignatureMethod=HmacSHA1&SignatureVersion=2&Timestamp=2011-06-29T17%3A40%3A49.000Z&Version=2009-04-15'
//               AWSAccessKeyId=REDACTED_ACCESS_KEY &Action=PutAttributes &Attribute.1.Name=TestName1-1 &Attribute.1.Value=TestValue1-1 &Attribute.2.Name=TestName2-2&Attribute.2.Value=TestValue2-2&DomainName=TESTDataLog2&ItemName=2011-06-29 17:01:51&SignatureMethod=HmacSHA1&SignatureVersion=2&Timestamp=2011-06-30T01%3A01%3A51.000Z&Version=2009-04-15&Signature=REDACTED
/**************** INTERNAL FUNCTIONS ************************/

static void escape_signature(unsigned char *in, unsigned char *out)
{
	int i1;
	int i2;
	int len=strlen((char*)in);
	
	for(i1=0, i2=0; i1<len; i1++)
	{
		switch(in[i1])
		{
			case '/':
				out[i2++]='%';
				out[i2++]='2';
				out[i2++]='F';
				break;
			case '=':
				out[i2++]='%';
				out[i2++]='3';
				out[i2++]='D';
				break;
			case '+':
				out[i2++]='%';
				out[i2++]='2';
				out[i2++]='B';
				break;
				
			default:
				out[i2++]=in[i1];
		}
	}
	out[i2-1]='\0';
}
			
			
static int calculate_signature(unsigned char *data_in, unsigned char *signature)
{
	int ret;
	int base64_length;
	
	HmacSetKey(&sdb_hmac, SHA, (const byte*)secret_key, 40);
	HmacUpdate(&sdb_hmac, (const byte*)data_in, strlen((char*)data_in));
    HmacFinal(&sdb_hmac, (byte*)sdb_digest);

	base64_length=64;
	ret = Base64Encode(sdb_digest, 20, signature, (word32*)&base64_length);
	signature[base64_length]='\0';

	return ret;
}

static unsigned char *print_timestamp(void)
{
	short year;
	char month;
	char day;
	char hour;
	char minute;
	char second;

	utc_get_time(&year, &month, &day, &hour, &minute, &second);
	sprintf((char*)sdb_timestamp, "%d-%s%d-%s%dT%s%d%%3A%s%d%%3A%s%d.000Z", year, month<10?"0":"", month,
										      							day<10?"0":"", day,
										      							hour<10?"0":"", hour,
										      							minute<10?"0":"", minute,
										      							second<10?"0":"", second);
	return sdb_timestamp;
}

static int generate_request(unsigned char *params, unsigned char *signature)
{
	// Calculate Request
	request[0]='\0';
	sprintf((char*)request+strlen((char*)request), "GET /?%s&Signature=", params);
	escape_signature(signature, request+strlen((char*)request));
	sprintf((char*)request+strlen((char*)request), " HTTP/1.1\r\nHost: %s\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nKeep-Alive: 115\r\nConnection: keep-alive\r\n\r\n", amazon_url);
	
	return  send_request(request);
}


/********* NETWORK STUFFS **********************/

static void sdb_close(struct tcp_pcb *pcb)
{
   	tcp_arg(pcb, NULL);
   	tcp_sent(pcb, NULL);
   	tcp_recv(pcb, NULL);
   	tcp_poll(pcb, NULL, 0);
   	tcp_close(pcb);
   	request_pcb=NULL;
	debug_printf("closing sdb connection\n");
}



static err_t sdb_received(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	struct pbuf *q;
	int i;
	
	if (callback_fn!=NULL) callback_fn(0);

	if (err == ERR_OK && p != NULL)
   	{
   		debug_printf("sdb_received %d bytes!\n", p->tot_len);
		tcp_recved(pcb, p->tot_len);

		for(q=p; q!=NULL; q=q->next)
		{
			for(i=0; i<q->len; i++)
			{
				debug_printf("%c", ((char*)q->payload)[i]);
			}
		}
		debug_printf("\n");
       	pbuf_free(p);
       	//sdb_close(pcb);
   }
   else
   {
       debug_printf("\nserver_recv(): Errors-> ");
       if (err != ERR_OK)
       {
			debug_printf("1) Connection is not on ERR_OK state, but in %d state->\n", err);
       }
       
       if (p == NULL)
       {
           debug_printf("2) Pbuf pointer p is a NULL pointer->\n ");
       }
       debug_printf("server_recv(): Closing server-side connection...");

       pbuf_free(p);
       sdb_close(pcb);
   }

   return ERR_OK;
}

static err_t sdb_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
   	LWIP_UNUSED_ARG(len);
   	LWIP_UNUSED_ARG(arg);

	debug_printf("sdb_sent %d bytes\n", len);

   	return ERR_OK;
}
#if 0
static err_t sdb_poll(void *arg, struct tcp_pcb *pcb)
{
	err_t ret;
	
	if (request[0]!='\0')
	{
		//debug_printf("sending: %s\n", request);
		ret=tcp_write(pcb, request, strlen((char*)request), TCP_WRITE_FLAG_COPY);
		if (ret!=ERR_OK)
		{
			debug_printf("tcp_write doesn't return ERR_OK: %d\n", ret);
		} 
		request[0]='\0';
	}

	return ERR_OK;
}
#endif
static err_t sdb_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
	err_t ret;
	debug_printf("sdb_connected!!\n");
   	if (err != ERR_OK)
   	{
		debug_printf("sdb_connected: err=%d\n", err);
		return err;
   	}
   	else
   	{
   		//debug_printf("Sending: %s\n", request);
		tcp_sent(pcb, sdb_sent);
		//tcp_poll(pcb, sdb_poll, 0);
		tcp_poll(pcb, NULL, 0);
		tcp_recv(pcb, sdb_received);
       	ret=tcp_write(pcb, request, strlen((char*)request), TCP_WRITE_FLAG_COPY);
       	if (ret!=ERR_OK)
       	{
       		debug_printf("tc_write doesn't return ERR_OK: %d\n", ret);
       	}
       	request[0]='\0';
   }
   	return err;
}



static int send_request(unsigned char *str)
{
	struct ip_addr dest;
	err_t ret;
	
	
	// create a new connection if needed
	if (request_pcb==NULL)
	{
		request_pcb = tcp_new();
		if (request_pcb==NULL)
		{
			debug_printf("amazon_sdb: tcp_new returns NULL!\n");
			return -1;
		}
	}

	if (request_pcb->state!=ESTABLISHED)
	{	
		// set the arg values
		tcp_arg(request_pcb, NULL);
		
		debug_printf("sdb: attempting active connection\n");
		IP4_ADDR(&dest, 204, 246, 162, 148);
		ret = tcp_connect(request_pcb, &dest, 80, sdb_connected); 
		if (ret!=ERR_OK)
		{
			debug_printf("amazon_sdb: tcp_connect returns %d\n", ret);
			return -2;
		}
	}
	else
	{
		//debug_printf("sending: %s\n", request);
		ret=tcp_write(request_pcb, request, strlen((char*)request), TCP_WRITE_FLAG_COPY);
		if (ret!=ERR_OK)
		{
			debug_printf("tcp_write doesn't return ERR_OK: %d\n", ret);
		} 
		request[0]='\0';
	}
	return 0;
}

/***   End Of File   ***/
