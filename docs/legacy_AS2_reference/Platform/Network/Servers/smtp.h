#ifndef SMTP_H
#define SMTP_H

#define SMTP_IDLE           0x500
#define SMTP_CONNECTING     0x501
#define SMTP_GREETING       0x502
#define SMTP_MAIL_FROM      0x503
#define SMTP_MAIL_TO        0x504
#define SMTP_DATA           0x505
#define SMTP_DATA_HEAD      0x506
#define SMTP_DATA_BODY      0x507
#define SMTP_DONE           0x508
#define SMTP_QUIT           0x509

#define SMTP_ERROR        0x5FF

int smtp_send(char *subject, char *body, char *to_addr, char *from_addr, struct ip_addr server, short server_port);
int smtp_status(void);



#endif
/***   End Of File   ***/
