#ifndef SMTP_BLOCKING_H_
#define SMTP_BLOCKING_H_


#define SMTP_SERVER_MAX_LENGTH      30
#define SMTP_RETURN_ADDR_MAX_LENGTH 30
#define SMTP_HOST_ID_MAX_LENGTH     30
#define SMTP_USERNAME_LENGTH		30
#define SMTP_PASSWORD_LENGTH		30

typedef struct
{
    char           server[SMTP_SERVER_MAX_LENGTH];
    unsigned short port;
    char           return_addr[SMTP_RETURN_ADDR_MAX_LENGTH];
    char           host_id[SMTP_HOST_ID_MAX_LENGTH];
    char		   username[SMTP_USERNAME_LENGTH];
    char           password[SMTP_PASSWORD_LENGTH];
    char           auth; // 0 =none, 1 =yes
    char           ssl; // 0 =none, 1 =yes.  If ssl, then auth is implied
}_smtp_params;

int smtp_init(_smtp_params *smtp_params);
int smtp_start_send(char *to, char *from, int priority, const char *subject, ...);
int smtp_send_body_line(char *line);
int smtp_printf(const char *format, ...);
int smtp_send_end(void);


#endif
