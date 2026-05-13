#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "system.h"
#include "debug.h"

#include "utils/lwiplib.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"

#include "lwip/tcp.h"
#include "utils/lwiplib.h"
#include "ssl.h"
#include "tools.h"

#include "version.h"
#include "smtp_blocking.h"

static ip_addr_t remoteaddr={0};
static char smtp_temp_str[1000];
static char smtp_temp2_str[1000];
static int socket_fd;
static _smtp_params *smtp_params;

static SSL_CTX *smtp_ctx;
static SSL *smtp_ssl=NULL;

static void smtp_dns_found_cb(const char *name, struct ip_addr *ipaddr, void *callback_arg);
static int _smtp_get_response(unsigned int timeout_ms);
static int _smtp_send_line(char *line, unsigned int timeout_ms);
static int _smtp_generate_credentials(char *dest);

int smtp_init(_smtp_params *smtp_params_in)
{
	unsigned int addr_state;

	smtp_params=smtp_params_in;

	// Get Remote Address
	addr_state = ipaddr_addr(smtp_params->server);
	if (addr_state == IPADDR_NONE)
	{
		dns_gethostbyname(smtp_params->server, &remoteaddr, smtp_dns_found_cb, NULL, 1);
		debug_printf("smtp: making DNS call for '%s'\r\n", smtp_params->server);
	}
	else
	{
		ip4_addr_set_u32(&remoteaddr, addr_state);
		debug_printf("smtp: in IP address form already\n");
	}

	debug_printf("Creating new CTX: ");
	if ((smtp_ctx = ssl_ctx_new(SSL_DISPLAY_CERTS | SSL_DISPLAY_STATES | SSL_SERVER_VERIFY_LATER, 1))==NULL)
	{
		debug_printf("ERROR: Cannot allocate Data Server CTS\n");
	}
	else
	{
		debug_printf("Successful\r\n");
	}

	return 0;
}


#define SMTP_INVALID_ADDRESS	-3
#define SMTP_SOCKET_FAILED		-1
#define SMTP_BIND_FAILED		-2
#define SMTP_FAILED_CONNECTING1	-4
#define SMTP_FAILED_CONNECTING2	-5
#define SMTP_FAILED_CONNECTING3	-6
#define SMTP_FAILED_SERVER1		-7
#define SMTP_SERVER_HELO_ERROR	-8
#define SMTP_STARTTLS_FAILURE	-9
#define SMTP_TLS_FAILURE		-10
#define SMTP_AUTH_FAILURE		-11
#define SMTP_SERVER_FROM_ERROR	-12
#define SMTP_SERVER_TO_ERROR	-13
#define SMTP_SERVER_DATA_ERROR	-14
#define SMTP_SERVER_END_ERROR	-15

static char body_line[1000];

int smtp_start_send(char *to, char *from, int priority, const char *subject, ...)
{
	struct sockaddr_in ra;
	int code;
	int ret=0;
	int connect_attempts=0;
	char *p1;
	char *p2;
	va_list ap;
	int error;


	va_start(ap, subject); // Start the varargs processing.
	vsprintf(body_line, subject, ap);
	va_end(ap); // We're finished with the varargs now.


	if (remoteaddr.addr==0)
	{
		debug_printf("smtp: cannot send, no valid address\r\n");
		return SMTP_INVALID_ADDRESS;
	}

	debug_printf("Creating socket: ");
	if ((socket_fd = lwip_socket(PF_INET, SOCK_STREAM, 0))<0)
	{
		debug_printf("smtp: failed to get netconn\r\n");
		return SMTP_SOCKET_FAILED;
	}
	debug_printf("Success (%d)\r\n", socket_fd);

	memset(&ra, 0, sizeof(struct sockaddr_in));
	ra.sin_family = AF_INET;
	ra.sin_addr.s_addr = remoteaddr.addr;
	ra.sin_port = htons(smtp_params->port);

	do
	{
		if (connect_attempts++>3)
		{
			debug_printf("smtp: failed connecting, giving up\r\n");
			ret = SMTP_FAILED_CONNECTING1;
			goto smtp_wrapup;
		}

		debug_printf("smtp: connecting to %d.%d.%d.%d\r\n", ip4_addr1(&remoteaddr),ip4_addr2(&remoteaddr),ip4_addr3(&remoteaddr),ip4_addr4(&remoteaddr));
		if ((error=lwip_connect(socket_fd, (const struct sockaddr*)&ra, sizeof(struct sockaddr_in))) < 0)
		{
			debug_printf("smtp: failed to connect, %d\r\n", error);
			ret = SMTP_FAILED_CONNECTING2;
			goto smtp_disconnect;
		}


		debug_printf("smtp: Connected!!\r\n");
		debug_printf("smtp: Wait for server hello\r\n");

		if ((code=_smtp_get_response(10000))!=220)
		{
			if (code==421)
			{
				debug_printf("smtp: Service not ready, delaying and trying again\r\n");
				vTaskDelay(1000/portTICK_RATE_MS);
				lwip_close(socket_fd);
			}
			else
			{
				debug_printf("smtp: unhandled code: %d\r\n", code);
				ret = SMTP_FAILED_CONNECTING3;
				goto smtp_disconnect;
			}
		}
	}while(code!=220);

	// say Hello and introduce ourselves
	sprintf(smtp_temp_str, "EHLO %s", smtp_params->host_id);
	if (_smtp_send_line(smtp_temp_str, 10000) != 250)
	{
		ret = SMTP_SERVER_HELO_ERROR;
		goto smtp_disconnect;
	}

	if (smtp_params->ssl)
	{
		debug_printf("smtp: starting tls\r\n");
		if ((ret=_smtp_send_line("STARTTLS", 10000))!=220)
		{
			ret = SMTP_STARTTLS_FAILURE;
			goto smtp_disconnect;
		}

		debug_printf("smtp: creating ssl connection: ");
		if ((smtp_ssl=ssl_client_new(smtp_ctx, socket_fd, NULL, 0))==NULL)
		{
			debug_printf("Failed\r\n");
			ret = SMTP_TLS_FAILURE;
			goto smtp_disconnect;
		}
		debug_printf("Success\r\n");


		smtp_params->auth=1; // for authentication if doing ssl
	}

	if (smtp_params->auth)
	{
		debug_printf("smtp: doing authentication\r\n");
		ret = _smtp_generate_credentials(smtp_temp_str);
		base64_encode((unsigned char *)smtp_temp_str, ret, smtp_temp2_str, 1000);
		sprintf(smtp_temp_str, "AUTH PLAIN %s\r\n", smtp_temp2_str);
		if ((ret=_smtp_send_line(smtp_temp_str, 10000))!=235)
		{
			ret = SMTP_AUTH_FAILURE;
			goto smtp_disconnect;
		}
	}


	// Mail FROM
	sprintf(smtp_temp_str, "MAIL FROM: <%s>", from);
	if (_smtp_send_line(smtp_temp_str, 10000)==0)// timout
	{
		ret = SMTP_SERVER_FROM_ERROR;
		goto smtp_disconnect;
	}

	// Mail TO
	memset(smtp_temp2_str, 0, 1000);
	strcpy(smtp_temp2_str, to);
	p1=strtok(smtp_temp2_str, ",");
	while((p1!=NULL)&&(*p1!='\0'))
	{
		p2=p1+strlen(p1)+1;
		sprintf(smtp_temp_str, "RCPT TO: <%s>", p1);
		if (_smtp_send_line(smtp_temp_str, 10000)==0) // timeout
		{
			ret = SMTP_SERVER_TO_ERROR;
			goto smtp_disconnect;
		}
		p1=strtok(p2, ",");
	}

	// DATA
	if (_smtp_send_line("DATA", 10000)==0) // timeout
	{
		ret = SMTP_SERVER_DATA_ERROR;
		goto smtp_disconnect;
	}


	// FROM
	sprintf(smtp_temp_str, "From: %s", from);
	_smtp_send_line(smtp_temp_str, 0);


	// TO
	memset(smtp_temp2_str, 0, 1000);
	strcpy(smtp_temp2_str, to);
	p1=strtok(smtp_temp2_str, ",");
	while((p1!=NULL)&&(*p1!='\0'))
	{
		p2=p1+strlen(p1)+1;
		sprintf(smtp_temp_str, "To: %s", p1);
		_smtp_send_line(smtp_temp_str, 0);
		p1=strtok(p2, ",");
	}


	// SUBJECT
	sprintf(smtp_temp_str, "Subject: %s", body_line);
	_smtp_send_line(smtp_temp_str, 0);

	// Priority
	sprintf(smtp_temp_str, "X-Priority: %d", priority);
	_smtp_send_line(smtp_temp_str, 0);

	return 0;

smtp_disconnect:
	if (smtp_ssl!=NULL) ssl_free(smtp_ssl);
	smtp_ssl=NULL;
	lwip_close(socket_fd);

smtp_wrapup:
	return ret;
}


int smtp_printf(const char *format, ...)
{
	va_list ap;

	// Start the varargs processing.
	va_start(ap, format);

	vsprintf(body_line, format, ap);
	_smtp_send_line(body_line, 0);

	// We're finished with the varargs now.
	va_end(ap);

	return 0;
}

int smtp_send_body_line(char *line)
{
	_smtp_send_line(line, 0);
	return 0;
}

int smtp_send_end(void)
{
	int ret=0;

	_smtp_send_line("", 0);
	if (_smtp_send_line(".", 10000)==0) // timeout
	{
		ret = SMTP_SERVER_END_ERROR;
	}

	if (smtp_ssl!=NULL) ssl_free(smtp_ssl);
	smtp_ssl=NULL;

	lwip_close(socket_fd);
	return ret;
}

static void smtp_dns_found_cb(const char *name, struct ip_addr *ipaddr, void *callback_arg)
{
	if ((ipaddr) && (ipaddr->addr))
  	{
  		memcpy(&remoteaddr, ipaddr, sizeof(struct ip_addr));
		debug_printf("smtp: dns_found: '%s' -> %d.%d.%d.%d\n\n", name, ip4_addr1(ipaddr),ip4_addr2(ipaddr),ip4_addr3(ipaddr),ip4_addr4(ipaddr));
  	}
	else
	{
		debug_printf("smtp: failed dns\r\n");
	}
}

static int _smtp_send_line(char *line, unsigned int timeout_ms)
{
	debug_printf("smtp: sending: %s\r\n", line);
	if (smtp_ssl!=NULL)
	{
		ssl_write(smtp_ssl, line, strlen(line));
		ssl_write(smtp_ssl, "\r\n", 2);
	}
	else
	{
		lwip_send(socket_fd, line, strlen(line), 0);
		lwip_send(socket_fd, "\r\n", 2, 0);
	}
	if (timeout_ms>0) return _smtp_get_response(timeout_ms);
	return 0;
}

static int _smtp_get_response(unsigned int timeout_ms)
{
	int bytes_received;
	unsigned int start_time = uptime_ms;
	char *data_ptr = smtp_temp2_str;

	while((start_time+timeout_ms)>uptime_ms)
	{
		memset(smtp_temp2_str, 0, 1000);
		if (smtp_ssl!=NULL) bytes_received = ssl_read(smtp_ssl, (unsigned char**)&data_ptr);
		else                bytes_received = lwip_recv(socket_fd, smtp_temp2_str, 1000, 0);

		if (bytes_received<0)
		{
			debug_printf("lwip_recv/ssl_read errored with %d\r\n", errno);
			return -1;
		}
		debug_printf("bytes_received: %d\r\n", bytes_received);

		if (bytes_received>0)
		{
			data_ptr[bytes_received]='\0';
			debug_printf("smtp: received: %s\r\n", data_ptr);

			data_ptr[3]='\0';
			return atoi(data_ptr);
		}
	}

	debug_printf("smtp: recv timeout\r\n");

	return -1;
}

static int _smtp_generate_credentials(char *dest)
{
	int i;
	int j = 0;

	i = strlen(smtp_params->username);
	j = strlen(smtp_params->password);

	memcpy(dest, smtp_params->username, i);
	dest[i] = '\0';
	memcpy((dest + i + 1), smtp_params->username, i);
	dest[(i * 2) + 1] = '\0';
	memcpy((dest + (i * 2) + 2), smtp_params->password, j);

	return (i * 2) + j + 2;
}

/***   End Of File   ***/
