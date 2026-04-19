import os

def is_in_production():
    if 'WEBSITE_HOSTNAME' in os.environ:
        return True
    return False

def get_env_variable(value):
    os_variable = os.environ.get(value, None)
    if(is_in_production and os_variable is not None):
        return os_variable

    from .secrets_dev import connection_strings
    return connection_strings[value]

# IOT_HUB_CONNECTION_SERVICE
iot_hub_connection_string = get_env_variable('CUSTOMCONNSTR_IOT_HUB_SERVICE_CONNECTION')
# IOT_HUB_CONNECTION_HUBOWNER
iot_hub_connection_string_hubowner = get_env_variable('CUSTOMCONNSTR_IOT_HUB_OWNER_CONNECTION')

# API_KEY_for_view_GetIoTHubConnectionString
api_key_for_view_GetIoTHubConnectionString = get_env_variable('CUSTOMCONNSTR_API_KEY_GetIoTHubConnectionString')

# DB_PASS
azure_db_pass = get_env_variable('POSTGRESQLCONNSTR_DB_PASS')
