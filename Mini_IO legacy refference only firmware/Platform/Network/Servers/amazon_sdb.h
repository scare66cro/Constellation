#ifndef AMAZON_SDB_H
#define AMAZON_SDB_H

#define MAX_ATTRIBUTE_LEN	30

typedef void (*completed_cb)(int);

int list_domain(int max_domains, completed_cb cb);
int create_domain(char *name, completed_cb cb);
int put_attribute(char *domain_name, char *item_name, char attribute_name[][MAX_ATTRIBUTE_LEN], char attribute_value[][MAX_ATTRIBUTE_LEN], int num_attributes, completed_cb callback);
void cancel_request(void);

#endif
/***   End Of File ***/
