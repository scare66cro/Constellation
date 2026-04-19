"""
psycopg2 compatibility shim for psycopg3

This module provides minimal psycopg2-compatible API using psycopg3.
For local development only - production uses actual psycopg2.
"""
import psycopg

# Re-export psycopg3's Error as psycopg2.Error
Error = psycopg.Error
DatabaseError = psycopg.DatabaseError
InterfaceError = psycopg.InterfaceError
OperationalError = psycopg.OperationalError
ProgrammingError = psycopg.ProgrammingError


def connect(dbname=None, host=None, user=None, password=None, port=5432, **kwargs):
    """
    Connect to PostgreSQL using psycopg3 with psycopg2-style arguments.
    """
    conninfo = f"host={host} port={port} dbname={dbname} user={user} password={password}"
    return psycopg.connect(conninfo, **kwargs)
