"""
psycopg2 compatibility shim for psycopg3

This module provides minimal psycopg2-compatible API using psycopg3.
For local development only - production uses actual psycopg2.
"""
import psycopg

# Re-export psycopg3's Error classes as psycopg2.Error
Error = psycopg.Error
Warning = Warning  # Built-in Warning
InterfaceError = psycopg.InterfaceError
DatabaseError = psycopg.DatabaseError
DataError = psycopg.DataError
OperationalError = psycopg.OperationalError
IntegrityError = psycopg.IntegrityError
InternalError = psycopg.InternalError
ProgrammingError = psycopg.ProgrammingError
NotSupportedError = psycopg.NotSupportedError


def connect(dbname=None, host=None, user=None, password=None, port=5432, **kwargs):
    """
    Connect to PostgreSQL using psycopg3 with psycopg2-style arguments.
    """
    conninfo = f"host={host} port={port} dbname={dbname} user={user} password={password}"
    return psycopg.connect(conninfo, **kwargs)
