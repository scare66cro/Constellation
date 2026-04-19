"""
Location Database Service

This module provides functionality to interact with the location database
for retrieving location IDs, names, and customer information.
"""
import psycopg2
from django.conf import settings
import logging

logger = logging.getLogger(__name__)


class LocationAPIService:
    """Service class for interacting with the location database."""
    
    def __init__(self):
        self.db_config = {
            'dbname': settings.LOCATION_DBNAME,
            'host': settings.LOCATION_DBHOST,
            'user': settings.LOCATION_DBUSER,
            'password': settings.LOCATION_DBPASS,
            'port': getattr(settings, 'LOCATION_DBPORT', 5432)
        }
    
    def _get_connection(self):
        """Create and return a database connection."""
        try:
            conn = psycopg2.connect(**self.db_config)
            return conn
        except psycopg2.Error as e:
            logger.error(f"Database connection error: {e}")
            return None
    
    def get_all_locations(self):
        """
        Retrieve all locations from the database.
        
        Returns:
            list: A list of dictionaries containing location data with 'id' and 'name' keys.
                  Returns empty list if request fails.
        
        Example response:
            [
                {'id': 1, 'name': 'Customer A: Location 1'},
                {'id': 2, 'name': 'Location 2'},
                ...
            ]
        """
        conn = self._get_connection()
        if not conn:
            return []
        
        try:
            with conn.cursor() as cursor:
                # Query to get id, location_name, and customer_name
                query = """
                    SELECT id, location_name, customer_name 
                    FROM locations 
                    ORDER BY customer_name NULLS LAST, location_name
                """
                cursor.execute(query)
                rows = cursor.fetchall()
                
                locations = []
                for row in rows:
                    location_id, location_name, customer_name = row
                    
                    # Format display name based on customer_name availability
                    if customer_name:
                        display_name = f"{customer_name}: {location_name}"
                    else:
                        display_name = location_name
                    
                    locations.append({
                        'id': location_id,
                        'name': display_name,
                        'location_name': location_name,
                        'customer_name': customer_name
                    })
                
                logger.info(f"Successfully retrieved {len(locations)} locations from database")
                return locations
                
        except psycopg2.Error as e:
            logger.error(f"Database query error: {e}")
            return []
        finally:
            conn.close()
    
    def get_location_by_id(self, location_id):
        """
        Retrieve a specific location by ID from the database.
        
        Args:
            location_id (int): The ID of the location to retrieve.
        
        Returns:
            dict or None: Dictionary containing location data with 'id' and 'name' keys.
                         Returns None if location not found or request fails.
        """
        conn = self._get_connection()
        if not conn:
            return None
        
        try:
            with conn.cursor() as cursor:
                query = """
                    SELECT id, location_name, customer_name 
                    FROM locations 
                    WHERE id = %s
                """
                cursor.execute(query, (location_id,))
                row = cursor.fetchone()
                
                if row:
                    location_id, location_name, customer_name = row
                    
                    # Format display name based on customer_name availability
                    if customer_name:
                        display_name = f"{customer_name}: {location_name}"
                    else:
                        display_name = location_name
                    
                    return {
                        'id': location_id,
                        'name': display_name,
                        'location_name': location_name,
                        'customer_name': customer_name
                    }
                return None
                
        except psycopg2.Error as e:
            logger.error(f"Database query error: {e}")
            return None
        finally:
            conn.close()
    
    def get_location_names(self):
        """
        Retrieve only the names of all locations.
        
        Returns:
            list: A list of location names.
        """
        locations = self.get_all_locations()
        return [location.get('name', '') for location in locations if location.get('name')]
    
    def get_location_ids(self):
        """
        Retrieve only the IDs of all locations.
        
        Returns:
            list: A list of location IDs.
        """
        locations = self.get_all_locations()
        return [location.get('id') for location in locations if location.get('id') is not None]


# Convenience function for easy imports
def get_location_service():
    """Factory function to create a LocationAPIService instance."""
    return LocationAPIService()
