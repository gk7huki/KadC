# This file was created automatically by SWIG.
# Don't modify this file, modify the SWIG interface instead.
# This file is compatible with both classic and new-style classes.

import _KadCmodule

def _swig_setattr(self,class_type,name,value):
    if (name == "this"):
        if isinstance(value, class_type):
            self.__dict__[name] = value.this
            if hasattr(value,"thisown"): self.__dict__["thisown"] = value.thisown
            del value.thisown
            return
    method = class_type.__swig_setmethods__.get(name,None)
    if method: return method(self,value)
    self.__dict__[name] = value

def _swig_getattr(self,class_type,name):
    method = class_type.__swig_getmethods__.get(name,None)
    if method: return method(self)
    raise AttributeError,name

import types
try:
    _object = types.ObjectType
    _newclass = 1
except AttributeError:
    class _object : pass
    _newclass = 0
del types


KADC_OK = _KadCmodule.KADC_OK
KADC_START_CANT_OPEN_INI_FILE = _KadCmodule.KADC_START_CANT_OPEN_INI_FILE
KADC_START_WSASTARTUP_ERROR = _KadCmodule.KADC_START_WSASTARTUP_ERROR
KADC_START_OVERNET_SECTION_INI_FILE_ERROR = _KadCmodule.KADC_START_OVERNET_SECTION_INI_FILE_ERROR
KADC_START_EMULEKAD_SECTION_INI_FILE_ERROR = _KadCmodule.KADC_START_EMULEKAD_SECTION_INI_FILE_ERROR
KADC_START_REVCONNECT_SECTION_INI_FILE_ERROR = _KadCmodule.KADC_START_REVCONNECT_SECTION_INI_FILE_ERROR
KADC_START_BAD_LOCALNODE_ADDRESS = _KadCmodule.KADC_START_BAD_LOCALNODE_ADDRESS
KADC_START_NO_MEMORY = _KadCmodule.KADC_START_NO_MEMORY
KADC_START_UDPIO_FAILED = _KadCmodule.KADC_START_UDPIO_FAILED
KADC_START_OVERNET_KADENGINE_FAILED = _KadCmodule.KADC_START_OVERNET_KADENGINE_FAILED
KADC_START_EMULEKAD_KADENGINE_FAILED = _KadCmodule.KADC_START_EMULEKAD_KADENGINE_FAILED
KADC_START_REVCONNECT_KADENGINE_FAILED = _KadCmodule.KADC_START_REVCONNECT_KADENGINE_FAILED
KADC_START_RTP_FAILED = _KadCmodule.KADC_START_RTP_FAILED
KADC_NEVER_STARTED = _KadCmodule.KADC_NEVER_STARTED
KADC_STOP_OVERNETINIFILEUPDATE_FAILED = _KadCmodule.KADC_STOP_OVERNETINIFILEUPDATE_FAILED
KADC_STOP_OVERNET_FAILED = _KadCmodule.KADC_STOP_OVERNET_FAILED
KADC_STOP_EMULEKAD_FAILED = _KadCmodule.KADC_STOP_EMULEKAD_FAILED
KADC_STOP_REVCONNECT_FAILED = _KadCmodule.KADC_STOP_REVCONNECT_FAILED
KADC_STOP_RTP_FAILED = _KadCmodule.KADC_STOP_RTP_FAILED
KADC_STOP_UDPIO_FAILED = _KadCmodule.KADC_STOP_UDPIO_FAILED
KADC_STOP_CANT_CREATE_NEW_INI_FILE = _KadCmodule.KADC_STOP_CANT_CREATE_NEW_INI_FILE
class KadCcontext(_object):
    __swig_setmethods__ = {}
    __setattr__ = lambda self, name, value: _swig_setattr(self, KadCcontext, name, value)
    __swig_getmethods__ = {}
    __getattr__ = lambda self, name: _swig_getattr(self, KadCcontext, name)
    def __repr__(self):
        return "<C KadCcontext instance at %s>" % (self.this,)
    __swig_setmethods__["s"] = _KadCmodule.KadCcontext_s_set
    __swig_getmethods__["s"] = _KadCmodule.KadCcontext_s_get
    if _newclass:s = property(_KadCmodule.KadCcontext_s_get, _KadCmodule.KadCcontext_s_set)
    __swig_setmethods__["errmsg1"] = _KadCmodule.KadCcontext_errmsg1_set
    __swig_getmethods__["errmsg1"] = _KadCmodule.KadCcontext_errmsg1_get
    if _newclass:errmsg1 = property(_KadCmodule.KadCcontext_errmsg1_get, _KadCmodule.KadCcontext_errmsg1_set)
    __swig_setmethods__["errmsg2"] = _KadCmodule.KadCcontext_errmsg2_set
    __swig_getmethods__["errmsg2"] = _KadCmodule.KadCcontext_errmsg2_get
    if _newclass:errmsg2 = property(_KadCmodule.KadCcontext_errmsg2_get, _KadCmodule.KadCcontext_errmsg2_set)
    __swig_setmethods__["pul"] = _KadCmodule.KadCcontext_pul_set
    __swig_getmethods__["pul"] = _KadCmodule.KadCcontext_pul_get
    if _newclass:pul = property(_KadCmodule.KadCcontext_pul_get, _KadCmodule.KadCcontext_pul_set)
    __swig_setmethods__["pOKE"] = _KadCmodule.KadCcontext_pOKE_set
    __swig_getmethods__["pOKE"] = _KadCmodule.KadCcontext_pOKE_get
    if _newclass:pOKE = property(_KadCmodule.KadCcontext_pOKE_get, _KadCmodule.KadCcontext_pOKE_set)
    __swig_setmethods__["pEKE"] = _KadCmodule.KadCcontext_pEKE_set
    __swig_getmethods__["pEKE"] = _KadCmodule.KadCcontext_pEKE_get
    if _newclass:pEKE = property(_KadCmodule.KadCcontext_pEKE_get, _KadCmodule.KadCcontext_pEKE_set)
    __swig_setmethods__["pRKE"] = _KadCmodule.KadCcontext_pRKE_set
    __swig_getmethods__["pRKE"] = _KadCmodule.KadCcontext_pRKE_get
    if _newclass:pRKE = property(_KadCmodule.KadCcontext_pRKE_get, _KadCmodule.KadCcontext_pRKE_set)
    __swig_setmethods__["inifilename"] = _KadCmodule.KadCcontext_inifilename_set
    __swig_getmethods__["inifilename"] = _KadCmodule.KadCcontext_inifilename_get
    if _newclass:inifilename = property(_KadCmodule.KadCcontext_inifilename_get, _KadCmodule.KadCcontext_inifilename_set)
    __swig_setmethods__["inifile"] = _KadCmodule.KadCcontext_inifile_set
    __swig_getmethods__["inifile"] = _KadCmodule.KadCcontext_inifile_get
    if _newclass:inifile = property(_KadCmodule.KadCcontext_inifile_get, _KadCmodule.KadCcontext_inifile_set)
    __swig_setmethods__["wfile"] = _KadCmodule.KadCcontext_wfile_set
    __swig_getmethods__["wfile"] = _KadCmodule.KadCcontext_wfile_get
    if _newclass:wfile = property(_KadCmodule.KadCcontext_wfile_get, _KadCmodule.KadCcontext_wfile_set)
    def __init__(self, *args):
        _swig_setattr(self, KadCcontext, 'this', _KadCmodule.new_KadCcontext(*args))
        _swig_setattr(self, KadCcontext, 'thisown', 1)
    def __del__(self, destroy=_KadCmodule.delete_KadCcontext):
        try:
            if self.thisown: destroy(self)
        except: pass

class KadCcontextPtr(KadCcontext):
    def __init__(self, this):
        _swig_setattr(self, KadCcontext, 'this', this)
        if not hasattr(self,"thisown"): _swig_setattr(self, KadCcontext, 'thisown', 0)
        _swig_setattr(self, KadCcontext,self.__class__,KadCcontext)
_KadCmodule.KadCcontext_swigregister(KadCcontextPtr)


KadC_start = _KadCmodule.KadC_start

KadC_init_network = _KadCmodule.KadC_init_network

KadC_stop = _KadCmodule.KadC_stop

KadC_getnknodes = _KadCmodule.KadC_getnknodes

KadC_getncontacts = _KadCmodule.KadC_getncontacts

KadC_getfwstatus = _KadCmodule.KadC_getfwstatus

KadC_getextIP = _KadCmodule.KadC_getextIP

KadC_getourhashID = _KadCmodule.KadC_getourhashID

KadC_getourUDPport = _KadCmodule.KadC_getourUDPport

KadC_getourTCPport = _KadCmodule.KadC_getourTCPport

nodesinbucket = _KadCmodule.nodesinbucket

KadC_republish = _KadCmodule.KadC_republish

KadC_find = _KadCmodule.KadC_find
KADCTAG_INVALID = _KadCmodule.KADCTAG_INVALID
KADCTAG_NOTFOUND = _KadCmodule.KADCTAG_NOTFOUND
KADCTAG_HASH = _KadCmodule.KADCTAG_HASH
KADCTAG_STRING = _KadCmodule.KADCTAG_STRING
KADCTAG_ULONGINT = _KadCmodule.KADCTAG_ULONGINT
class KadCtag_iter(_object):
    __swig_setmethods__ = {}
    __setattr__ = lambda self, name, value: _swig_setattr(self, KadCtag_iter, name, value)
    __swig_getmethods__ = {}
    __getattr__ = lambda self, name: _swig_getattr(self, KadCtag_iter, name)
    def __repr__(self):
        return "<C KadCtag_iter instance at %s>" % (self.this,)
    __swig_setmethods__["tagsleft"] = _KadCmodule.KadCtag_iter_tagsleft_set
    __swig_getmethods__["tagsleft"] = _KadCmodule.KadCtag_iter_tagsleft_get
    if _newclass:tagsleft = property(_KadCmodule.KadCtag_iter_tagsleft_get, _KadCmodule.KadCtag_iter_tagsleft_set)
    __swig_setmethods__["pb"] = _KadCmodule.KadCtag_iter_pb_set
    __swig_getmethods__["pb"] = _KadCmodule.KadCtag_iter_pb_get
    if _newclass:pb = property(_KadCmodule.KadCtag_iter_pb_get, _KadCmodule.KadCtag_iter_pb_set)
    __swig_setmethods__["bufend"] = _KadCmodule.KadCtag_iter_bufend_set
    __swig_getmethods__["bufend"] = _KadCmodule.KadCtag_iter_bufend_get
    if _newclass:bufend = property(_KadCmodule.KadCtag_iter_bufend_get, _KadCmodule.KadCtag_iter_bufend_set)
    __swig_setmethods__["khash"] = _KadCmodule.KadCtag_iter_khash_set
    __swig_getmethods__["khash"] = _KadCmodule.KadCtag_iter_khash_get
    if _newclass:khash = property(_KadCmodule.KadCtag_iter_khash_get, _KadCmodule.KadCtag_iter_khash_set)
    __swig_setmethods__["vhash"] = _KadCmodule.KadCtag_iter_vhash_set
    __swig_getmethods__["vhash"] = _KadCmodule.KadCtag_iter_vhash_get
    if _newclass:vhash = property(_KadCmodule.KadCtag_iter_vhash_get, _KadCmodule.KadCtag_iter_vhash_set)
    __swig_setmethods__["tagtype"] = _KadCmodule.KadCtag_iter_tagtype_set
    __swig_getmethods__["tagtype"] = _KadCmodule.KadCtag_iter_tagtype_get
    if _newclass:tagtype = property(_KadCmodule.KadCtag_iter_tagtype_get, _KadCmodule.KadCtag_iter_tagtype_set)
    __swig_setmethods__["tagname"] = _KadCmodule.KadCtag_iter_tagname_set
    __swig_getmethods__["tagname"] = _KadCmodule.KadCtag_iter_tagname_get
    if _newclass:tagname = property(_KadCmodule.KadCtag_iter_tagname_get, _KadCmodule.KadCtag_iter_tagname_set)
    __swig_setmethods__["tagvalue"] = _KadCmodule.KadCtag_iter_tagvalue_set
    __swig_getmethods__["tagvalue"] = _KadCmodule.KadCtag_iter_tagvalue_get
    if _newclass:tagvalue = property(_KadCmodule.KadCtag_iter_tagvalue_get, _KadCmodule.KadCtag_iter_tagvalue_set)
    def __init__(self, *args):
        _swig_setattr(self, KadCtag_iter, 'this', _KadCmodule.new_KadCtag_iter(*args))
        _swig_setattr(self, KadCtag_iter, 'thisown', 1)
    def __del__(self, destroy=_KadCmodule.delete_KadCtag_iter):
        try:
            if self.thisown: destroy(self)
        except: pass

class KadCtag_iterPtr(KadCtag_iter):
    def __init__(self, this):
        _swig_setattr(self, KadCtag_iter, 'this', this)
        if not hasattr(self,"thisown"): _swig_setattr(self, KadCtag_iter, 'thisown', 0)
        _swig_setattr(self, KadCtag_iter,self.__class__,KadCtag_iter)
_KadCmodule.KadCtag_iter_swigregister(KadCtag_iterPtr)


KadCtag_begin = _KadCmodule.KadCtag_begin

KadCtag_next = _KadCmodule.KadCtag_next

KadCtag_find = _KadCmodule.KadCtag_find

KadCdictionary_dump = _KadCmodule.KadCdictionary_dump

KadCdictionary_destroy = _KadCmodule.KadCdictionary_destroy
class KadC_version_t(_object):
    __swig_setmethods__ = {}
    __setattr__ = lambda self, name, value: _swig_setattr(self, KadC_version_t, name, value)
    __swig_getmethods__ = {}
    __getattr__ = lambda self, name: _swig_getattr(self, KadC_version_t, name)
    def __repr__(self):
        return "<C KadC_version_t instance at %s>" % (self.this,)
    __swig_getmethods__["minor"] = _KadCmodule.KadC_version_t_minor_get
    if _newclass:minor = property(_KadCmodule.KadC_version_t_minor_get)
    __swig_getmethods__["major"] = _KadCmodule.KadC_version_t_major_get
    if _newclass:major = property(_KadCmodule.KadC_version_t_major_get)
    __swig_getmethods__["patchlevel"] = _KadCmodule.KadC_version_t_patchlevel_get
    if _newclass:patchlevel = property(_KadCmodule.KadC_version_t_patchlevel_get)
    def __init__(self, *args):
        _swig_setattr(self, KadC_version_t, 'this', _KadCmodule.new_KadC_version_t(*args))
        _swig_setattr(self, KadC_version_t, 'thisown', 1)
    def __del__(self, destroy=_KadCmodule.delete_KadC_version_t):
        try:
            if self.thisown: destroy(self)
        except: pass

class KadC_version_tPtr(KadC_version_t):
    def __init__(self, this):
        _swig_setattr(self, KadC_version_t, 'this', this)
        if not hasattr(self,"thisown"): _swig_setattr(self, KadC_version_t, 'thisown', 0)
        _swig_setattr(self, KadC_version_t,self.__class__,KadC_version_t)
_KadCmodule.KadC_version_t_swigregister(KadC_version_tPtr)

cvar = _KadCmodule.cvar
KadC_version = cvar.KadC_version

