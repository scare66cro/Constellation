"""
Location API Views

This module provides Django REST Framework views for interacting with the external
location REST API service.
"""
from rest_framework import viewsets, status
from rest_framework.decorators import action
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework.exceptions import PermissionDenied
from dry_rest_permissions.generics import DRYPermissions
import logging

from ..location_service import LocationAPIService

logger = logging.getLogger(__name__)


class LocationAPIViewSet(viewsets.ViewSet):
    """
    ViewSet for interacting with the external location REST API.
    
    This ViewSet provides endpoints to retrieve location data from an external REST API.
    """
    permission_classes = (IsAuthenticated,)
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.location_service = LocationAPIService()
    
    def list(self, request):
        """
        Default list endpoint for locations.
        Returns all locations from the database.
        """
        return self.all_locations(request)
    
    @action(detail=False, methods=['get'])
    def all_locations(self, request):
        """
        Retrieve all locations from the database.
        
        Returns:
            Response: JSON response containing list of locations with 'id' and 'name' fields.
        
        Example response:
            {
                "results": [
                    {"id": 1, "name": "Customer A: Location 1"},
                    {"id": 2, "name": "Location 2"}
                ],
                "count": 2,
                "status": "success"
            }
        """
        try:
            locations = self.location_service.get_all_locations()
            return Response({
                'results': locations,  # Use 'results' for DRF compatibility
                'data': locations,     # Keep 'data' for backward compatibility
                'count': len(locations),
                'status': 'success'
            }, status=status.HTTP_200_OK)
        except Exception as e:
            logger.error(f"Error retrieving locations: {str(e)}")
            return Response({
                'results': [],
                'data': [],
                'count': 0,
                'status': 'error',
                'message': 'Failed to retrieve locations from database'
            }, status=status.HTTP_500_INTERNAL_SERVER_ERROR)
    
    @action(detail=False, methods=['get'])
    def location_names(self, request):
        """
        Retrieve only the names of all locations.
        
        Returns:
            Response: JSON response containing list of location names.
        """
        try:
            names = self.location_service.get_location_names()
            return Response({
                'data': names,
                'count': len(names),
                'status': 'success'
            }, status=status.HTTP_200_OK)
        except Exception as e:
            logger.error(f"Error retrieving location names: {str(e)}")
            return Response({
                'data': [],
                'count': 0,
                'status': 'error',
                'message': 'Failed to retrieve location names from external API'
            }, status=status.HTTP_500_INTERNAL_SERVER_ERROR)
    
    @action(detail=False, methods=['get'])
    def location_ids(self, request):
        """
        Retrieve only the IDs of all locations.
        
        Returns:
            Response: JSON response containing list of location IDs.
        """
        try:
            ids = self.location_service.get_location_ids()
            return Response({
                'data': ids,
                'count': len(ids),
                'status': 'success'
            }, status=status.HTTP_200_OK)
        except Exception as e:
            logger.error(f"Error retrieving location IDs: {str(e)}")
            return Response({
                'data': [],
                'count': 0,
                'status': 'error',
                'message': 'Failed to retrieve location IDs from external API'
            }, status=status.HTTP_500_INTERNAL_SERVER_ERROR)
    
    @action(detail=True, methods=['get'])
    def location_detail(self, request, pk=None):
        """
        Retrieve a specific location by ID.
        
        Args:
            pk: The location ID to retrieve.
        
        Returns:
            Response: JSON response containing location data or 404 if not found.
        """
        try:
            location_id = int(pk)
            location = self.location_service.get_location_by_id(location_id)
            
            if location:
                return Response({
                    'data': location,
                    'status': 'success'
                }, status=status.HTTP_200_OK)
            else:
                return Response({
                    'data': None,
                    'status': 'error',
                    'message': f'Location with ID {location_id} not found'
                }, status=status.HTTP_404_NOT_FOUND)
                
        except ValueError:
            return Response({
                'data': None,
                'status': 'error',
                'message': 'Invalid location ID format'
            }, status=status.HTTP_400_BAD_REQUEST)
        except Exception as e:
            logger.error(f"Error retrieving location detail: {str(e)}")
            return Response({
                'data': None,
                'status': 'error',
                'message': 'Failed to retrieve location detail from external API'
            }, status=status.HTTP_500_INTERNAL_SERVER_ERROR)
    
    @action(detail=False, methods=['get'])
    def api_status(self, request):
        """
        Check the status of the external location API.
        
        Returns:
            Response: JSON response indicating API status.
        """
        try:
            # Try to fetch locations to test API connectivity
            locations = self.location_service.get_all_locations()
            
            return Response({
                'status': 'available',
                'message': 'Location API is accessible',
                'location_count': len(locations)
            }, status=status.HTTP_200_OK)
            
        except Exception as e:
            logger.error(f"Location API status check failed: {str(e)}")
            return Response({
                'status': 'unavailable',
                'message': 'Location API is not accessible',
                'error': str(e)
            }, status=status.HTTP_503_SERVICE_UNAVAILABLE)
    
    @action(detail=False, methods=['get'])
    def search(self, request):
        """
        Search locations by name for autocomplete functionality.
        
        Query Parameters:
            q: Search query string
            limit: Maximum number of results to return (default: 10)
        
        Returns:
            Response: JSON response containing matching locations.
        """
        try:
            query = request.GET.get('q', '').strip()
            limit = int(request.GET.get('limit', 10))
            
            if not query:
                return Response({
                    'data': [],
                    'count': 0,
                    'status': 'success'
                }, status=status.HTTP_200_OK)
            
            # Get all locations and filter by query
            all_locations = self.location_service.get_all_locations()
            
            # Filter locations that contain the query string (case-insensitive)
            filtered_locations = []
            for location in all_locations:
                location_name = location.get('name', '')
                if query.lower() in location_name.lower():
                    filtered_locations.append(location)
                    if len(filtered_locations) >= limit:
                        break
            
            return Response({
                'data': filtered_locations,
                'count': len(filtered_locations),
                'status': 'success'
            }, status=status.HTTP_200_OK)
            
        except Exception as e:
            logger.error(f"Error searching locations: {str(e)}")
            return Response({
                'data': [],
                'count': 0,
                'status': 'error',
                'message': 'Failed to search locations'
            }, status=status.HTTP_500_INTERNAL_SERVER_ERROR)
