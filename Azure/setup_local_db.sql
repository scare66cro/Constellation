-- Agristar Local Development Database Setup
-- Run this in pgAdmin or psql after installing PostgreSQL
--
-- If using psql:
--   psql -U postgres -f setup_local_db.sql
--
-- If using pgAdmin:
--   Open Query Tool, paste this, and click Execute

-- Create the database
CREATE DATABASE agristar_local;

-- Create the user
CREATE USER agristar WITH PASSWORD 'localdevpassword';

-- Grant privileges
GRANT ALL PRIVILEGES ON DATABASE agristar_local TO agristar;

-- Connect to the new database and grant schema privileges
\c agristar_local
GRANT ALL ON SCHEMA public TO agristar;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON TABLES TO agristar;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON SEQUENCES TO agristar;

-- Verify
\echo 'Database setup complete!'
\echo 'You can now run: python manage.py migrate'
