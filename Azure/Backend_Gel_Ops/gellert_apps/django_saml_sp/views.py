"""
    Views for saml authentication
"""
import logging
import uuid
from django.http import HttpResponse, HttpResponseRedirect, \
    HttpResponseBadRequest, HttpResponseServerError
from django.conf import settings
from django.contrib.auth import login
from django.core.exceptions import PermissionDenied
from django.views.decorators.csrf import csrf_exempt
from onelogin.saml2.auth import OneLogin_Saml2_Auth
from onelogin.saml2.utils import OneLogin_Saml2_Utils
from .backends import SAMLServiceProviderBackend
from ..user_account_app.serializers import UserAccountSerializer

logger = logging.getLogger('django_saml_sp')
samlBackend = SAMLServiceProviderBackend()

def initiate_login(request):
    """
        Start the saml login
    """
    req = get_request_details(request)
    auth = OneLogin_Saml2_Auth(req, settings.SP_SETTINGS)
    return_url = request.GET.get('next', settings.LOGIN_REDIRECT_URL)
    result = auth.login(return_to=return_url)
    response = HttpResponseRedirect(result)
    return response

@csrf_exempt
def complete_login(request):
    """
        Complete the saml login
    """
    req_id = str(uuid.uuid4())
    req = get_request_details(request)
    client_ip = request.META.get('HTTP_X_FORWARDED_FOR', request.META.get('REMOTE_ADDR'))
    has_saml = 'SAMLResponse' in req['post_data']
    relay_present = 'RelayState' in req['post_data']
    logger.info("[%s] SAML complete_login start: method=%s path=%s host=%s has_saml=%s relay_present=%s user_authenticated=%s ip=%s", req_id, request.method, request.path, req['http_host'], has_saml, relay_present, getattr(request.user, 'is_authenticated', False), client_ip)
    if request.method != 'POST' or not has_saml:
        logger.warning("[%s] ACS invoked without SAMLResponse (method=%s). This is likely a direct navigation or misrouted request.", req_id, request.method)

    try:
        auth = OneLogin_Saml2_Auth(req, settings.SP_SETTINGS)

        # Process and capture SAML toolkit state
        auth.process_response()
        errors = auth.get_errors()
        last_reason = auth.get_last_error_reason()
        logger.debug("[%s] SAML processed: errors=%s last_reason=%s is_authenticated=%s", req_id, errors, last_reason, auth.is_authenticated())

        if not errors:
            if auth.is_authenticated():
                # Log minimal attribute info safely (no values)
                attr_keys = list((auth.get_attributes() or {}).keys())
                nameid = auth.get_nameid()
                nameid_masked = (nameid[:3] + '***' + nameid[-3:]) if nameid and len(nameid) > 6 else nameid
                logger.info("[%s] SAML auth OK: nameid=%s attr_keys=%s", req_id, nameid_masked, attr_keys)

                user = samlBackend.authenticate(saml_authentication=auth)
                login(request, user, 'django.contrib.auth.backends.ModelBackend')
                serialized_user = UserAccountSerializer(user, context={'request':request})
                if 'RelayState' in req['post_data'] and \
                            OneLogin_Saml2_Utils.get_self_url(req) != req['post_data']['RelayState']:
                    target = req['post_data']['RelayState']
                    logger.info("[%s] Redirecting to RelayState: %s", req_id, target)
                    return HttpResponseRedirect(
                        auth.redirect_to(target), serialized_user.data)
                logger.info("[%s] Redirecting to LOGIN_REDIRECT_URL: %s", req_id, settings.LOGIN_REDIRECT_URL)
                return HttpResponseRedirect(settings.LOGIN_REDIRECT_URL)
            else:
                logger.warning("[%s] SAML processed but not authenticated.", req_id)
                raise PermissionDenied()
        else:
            logger.error("[%s] SAML errors: %s; reason=%s", req_id, ', '.join(errors), last_reason)
            return HttpResponseBadRequest(
                "Error when processing SAML Response: %s" % (', '.join(errors)))
    except Exception:
        logger.exception("[%s] Exception during complete_login", req_id)
        raise

def metadata(request):
    """
        Get saml metadata
    """
    req = get_request_details(request)
    auth = OneLogin_Saml2_Auth(req, settings.SP_SETTINGS)

    saml_settings = auth.get_settings()

    spmetadata = saml_settings.get_sp_metadata()
    errors = saml_settings.validate_metadata(spmetadata)

    if errors:
        return HttpResponseServerError(content=', '.join(errors))
    else:
        return HttpResponse(content=spmetadata, content_type='text/xml')

def complete_logout(request):
    """
        Complete saml logout
    """
    name_id = session_index = name_id_format = name_id_nq = name_id_spnq = None
    if 'samlNameId' in request.session:
        name_id = request.session['samlNameId']
    if 'samlSessionIndex' in request.session:
        session_index = request.session['samlSessionIndex']
    if 'samlNameIdFormat' in request.session:
        name_id_format = request.session['samlNameIdFormat']
    if 'samlNameIdNameQualifier' in request.session:
        name_id_nq = request.session['samlNameIdNameQualifier']
    if 'samlNameIdSPNameQualifier' in request.session:
        name_id_spnq = request.session['samlNameIdSPNameQualifier']
    req = get_request_details(request)
    auth = OneLogin_Saml2_Auth(req, settings.SP_SETTINGS)
    return HttpResponseRedirect(
        auth.logout(
            name_id=name_id, session_index=session_index, nq=name_id_nq,
            name_id_format=name_id_format, spnq=name_id_spnq))

def get_request_details(request):
    """
        Get saml request details
    """
    return {
        'http_host': request.META.get('HTTP_X_FORWARDED_HOST', request.META['HTTP_HOST']),
        'script_name': request.META['PATH_INFO'],
        'get_data': request.GET.copy(),
        'post_data': request.POST.copy(),
        'https': 'on'
    }
