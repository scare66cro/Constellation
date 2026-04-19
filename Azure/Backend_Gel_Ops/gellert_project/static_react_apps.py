"""
    Handle delivery of react application
"""
import os
import requests
import logging
from django.views.decorators.csrf import ensure_csrf_cookie
from django.shortcuts import redirect, HttpResponse
from gellert_project.settings import (
    DEV_MACHINE,
    DEV_REACT_PORT,
    DEV_CA_BUNDLE,
    DEV_ALLOW_INSECURE_SSL,
)

logger = logging.getLogger(__name__)

@ensure_csrf_cookie
def storage_pwa_static(request):
    """
        Serve up storage app static content or for dev mode link to DEV_MACHINE instance
    """
    url = request.META['PATH_INFO']
    # print('incoming...', url)
    # print('_'*20, request.LANGUAGE_CODE)

    if 'WEBSITE_HOSTNAME' in os.environ:
        if url == '/storage-app/':
            # file = open('./frontend_apps/storage-app/index.html')
            file = open('./staticfiles/storage-app/index.html')
            response = HttpResponse(content=file)
            response['Content-Type'] = 'text/html'
            return response

        root_static_files = set(os.listdir('./staticfiles/storage-app/'))
        if url.split('/')[1] == 'storage-app' and url.split('/')[2] in root_static_files:
        # if url.split('/')[1] == 'storage-app' and url.split('/')[2] != 'static':
            return redirect('/static'+url)

        return redirect('/storage-app/')

    # for DEVELOPMENT only
    print('-'*20,'DEV mode','-'*20)
    api = f"{DEV_MACHINE}:{DEV_REACT_PORT}{request.META['PATH_INFO']}"
    print("calling...", api)
    verify_arg = True
    if DEV_CA_BUNDLE:
        verify_arg = DEV_CA_BUNDLE
    elif DEV_ALLOW_INSECURE_SSL:
        verify_arg = False
    try:
        response = requests.get(api, verify=verify_arg, timeout=10)
        response.raise_for_status()
    except requests.exceptions.SSLError as e:
        logger.error("SSL error contacting React dev server %s: %s", api, e)
        return HttpResponse("SSL handshake failed contacting dev server. See server logs.", status=502)
    except requests.exceptions.RequestException as e:
        logger.error("Error contacting React dev server %s: %s", api, e)
        return HttpResponse("Dev server unreachable.", status=502)

    content = HttpResponse(content=response.content)
    content['Content-Type'] = response.headers.get('Content-Type', 'text/html')
    return content
