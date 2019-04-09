#include <sys/cdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <sys/queue.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <tcl.h>

#ifdef DMALLOC
#include "dmalloc.h"
#endif

#include <foo/log.h>
#include <foo/pthread.h>
#include <foo/mem.h>
#include <foo/rbtree.h>
#include <foo/tcl.h>
#include <foo/snmpex.h>

#ifndef lint
static const char rcsid[] = 
	"$Id: tclnetsnmp.c,v 1.41.2.5 2016/03/24 09:52:03 swp Exp $";
#endif /* !lint */

static 
void __attribute__((__constructor__)) 
init() 
{
	init_snmp("snmpapp");
}
static 
void __attribute__((__destructor__)) 
fini() 
{
	snmp_close_sessions();
	snmp_shutdown("snmpapp");
}

static void NetsnmpSessionFree(Tcl_Obj *objPtr);
static void NetsnmpSessionDup(Tcl_Obj *objPtr, Tcl_Obj *copyPtr);
static void UpdateStringOfNetsnmpSession(Tcl_Obj *objPtr);
static int  SetNetsnmpSessionFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr);

static const Tcl_ObjType tclNetsnmpSessionType = {
        "netsnmp_session",		/* name */
        NetsnmpSessionFree,		/* void freeIntRepProc(Tcl_Obj *objPtr)                     */
        NetsnmpSessionDup,		/* void dupIntRepProc(Tcl_Obj *objPtr, Tcl_Obj *copyPtr)    */
        UpdateStringOfNetsnmpSession,	/* void updateStringProc(Tcl_Obj *objPtr)                   */
        SetNetsnmpSessionFromAny	/* int  setFromAnyProc(Tcl_Interp *interp, Tcl_Obj *objPtr) */
};

#define TCLOBJ_NETSNMPSESSION_PTR(obj)	(*(netsnmp_session **)&(obj).internalRep.otherValuePtr)

/*     Если tcl хочет увидеть объект заданного типа (например list), то может
 * поменять внутреннее представление объекта (скажем на list) без оглядки на
 * кол-во ссылок (obj->refCount), т.е. даже если объект "shared".
 *     В нашем случае нельзя ни при каких условиях позволять менять внутреннее
 * представление. Если я правильно понимаю - это единственный способ дать tcl-у
 * по рукам.
 */
static
void
NetsnmpSessionFree(Tcl_Obj *obj)
{
	if (obj->refCount) {
		Tcl_Panic("[Tcl_Panic()] %s(): obj = <%p, %s, %d, <%p>>", 
			__func__,
			obj, 
			obj->typePtr ? obj->typePtr->name : "", 
			obj->refCount,
			TCLOBJ_NETSNMPSESSION_PTR(*obj));
	}
#ifdef WITH_DEBUGLOG
        DLOG("%20s(): obj = <%p, %s, %d, <%p>>\n", __func__,
                obj, obj->typePtr ? obj->typePtr->name : "", obj->refCount,
                TCLOBJ_NETSNMPSESSION_PTR(*obj));
#endif
	if (TCLOBJ_NETSNMPSESSION_PTR(*obj)) {
		snmp_close(TCLOBJ_NETSNMPSESSION_PTR(*obj));
		TCLOBJ_NETSNMPSESSION_PTR(*obj) = NULL;
	}
	obj->typePtr = NULL;
}

static
void
NetsnmpSessionDup(Tcl_Obj *obj, Tcl_Obj *copy)
{
	netsnmp_session session, *sess;
	char *err_message;

	sess = NULL;
	if (TCLOBJ_NETSNMPSESSION_PTR(*obj)) {
		snmp_sess_init(&session);
		session.peername = TCLOBJ_NETSNMPSESSION_PTR(*obj)->peername;
		sess = snmp_open(&session);
		if (!sess) {
			snmp_error(&session, 0, 0, &err_message);
			goto L_panic_1;
		}
		sess->version = TCLOBJ_NETSNMPSESSION_PTR(*obj)->version;
		free(sess->community);
		sess->community = (u_char *)strdup((const char *)TCLOBJ_NETSNMPSESSION_PTR(*obj)->community);
		if (!sess->community) {
			err_message = "strdup(3) failure.";
			goto L_panic_2;
		}
		sess->community_len = TCLOBJ_NETSNMPSESSION_PTR(*obj)->community_len;
	}
	TCLOBJ_NETSNMPSESSION_PTR(*copy) = sess;
        copy->typePtr = &tclNetsnmpSessionType;
#ifdef WITH_DEBUGLOG
        DLOG("%20s(): obj = <%p, %s, %d, <%p>>, copy = <%p, %s, %d, <%p>>, bytes = %s, length = %d\n",
                __func__,
                 obj,  obj->typePtr ?  obj->typePtr->name : "",  obj->refCount, TCLOBJ_NETSNMPSESSION_PTR(*obj),
                copy, copy->typePtr ? copy->typePtr->name : "", copy->refCount, TCLOBJ_NETSNMPSESSION_PTR(*copy),
                obj->bytes, obj->length);
#endif
	return;

L_panic_2:
	snmp_close(sess);
L_panic_1:
	emsg_add("%s(): %s", __func__, err_message);
	emsg_log();
	Tcl_Panic("%s(): %s", __func__, err_message);
}

static
void
UpdateStringOfNetsnmpSession(Tcl_Obj *obj)
{
        netsnmp_session *session;
        char *s;
	int n = 0;

	session = TCLOBJ_NETSNMPSESSION_PTR(*obj);
	if (session) {
		n = strlen(session->peername) + 1 + 
			strlen(snmp_version_string(session->version)) + 1 +
				session->community_len;
		s = ckalloc(n + 1);
		if (!s)
			goto L_panic;
		snprintf(s, n + 1, "%s %s %s", 
				session->peername, 
				snmp_version_string(session->version),
				session->community);
        } else {
		s = ckalloc(n + 1);
		if (!s)
			goto L_panic;
		*s = 0;
	}
        Tcl_InvalidateStringRep(obj);
        obj->bytes = s;
        obj->length = n;
#ifdef WITH_DEBUGLOG
        DLOG("%20s(): obj = <%p, %s, %d, <%p>>, bytes = %s, length = %d\n",
                __func__, obj, obj->typePtr ? obj->typePtr->name : "", obj->refCount,
                TCLOBJ_NETSNMPSESSION_PTR(*obj), obj->bytes, obj->length);
#endif
	return;

L_panic:
	Tclemsg_Panic("%s(): unable allocate %d bytes.", __func__, n + 1);
}


static
int
SetNetsnmpSessionFromAny(Tcl_Interp *interp, Tcl_Obj *obj)
{
	emsg_set_barrier();
	emsg_add(
		"%s(): forbidden operation.\n"
		"    obj = <%p, %s, %d>, bytes = %s, length = %d", 
		__func__, 
		obj, obj->typePtr ? obj->typePtr->name : "", obj->refCount, 
		obj->bytes, obj->length
	);
        if (interp)
                Tclemsg_AppendResult(interp);
        emsg_clear();
        return TCL_ERROR;
}


/* set sess [netsnmp::session ?-v <snmpVersion>? ?-c <snmpCommunity>? ?-? <snmpAgent>]
 * netsnmp::get $sess $vbl
 */
static
int
netsnmp_sessionObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	static const char usage[] = "netsnmp::session ?(-version|-v) (1|2c)? ?(-community|-c) community? ?--|-? agent";
	int version = SNMP_VERSION_2c;
	const char *community = "public";
	netsnmp_session session, *sess = NULL;
	Tcl_Obj *obj;

	emsg_set_barrier();

	do {
		enum { xGET_OPT, xGET_OPT_ARG } x;
		enum { optVersion, optCommunity } optopt;
		const char *s;
		int i;

		x = xGET_OPT;
		for (i = 1; i < objc; i++) {
			s = Tcl_GetString(objv[i]);
			if (x == xGET_OPT) {
				if (*s != '-')
					break;
				s++;
				if (!*s || (*s == '-' && !s[1])) {
					i++;
					break;
				}
				if (!strcmp(s, "version") || !strcmp(s, "v")) {
					optopt = optVersion;
					x = xGET_OPT_ARG;
					continue;
				}
				if (!strcmp(s, "community") || !strcmp(s, "c")) {
					optopt = optCommunity;
					x = xGET_OPT_ARG;
					continue;
				}
				emsg_add("%s(): unknown option: -%s\n%s", __func__, s, usage);
				goto L_error_1;
			} else /* if (x == xGET_OPT_ARG) */ {
				if (optopt == optVersion) {
					if (!strcmp(s, "1"))
						version = SNMP_VERSION_1;
					else if (!strcmp(s, "2c"))
						version = SNMP_VERSION_2c;
					else {
						emsg_add("%s(): wrong/unsupported snmp version: %s.\n%s", 
								__func__, s, usage);
						goto L_error_1;
					}
				} else /* if (optopt == optCommunity) */
					community = s;
				x = xGET_OPT;
			}
		}
		objc -= i;
		objv += i;
	} while (0);

	if (objc != 1) {
		emsg_add("%s(): %s", __func__, usage);
		goto L_error_1;
	}
	
	snmp_sess_init(&session);
	session.peername = Tcl_GetString(objv[0]);
	sess = snmp_open(&session);
	if (!sess) {
		char *err_message;
		snmp_error(&session, 0, 0, &err_message);
		emsg_add("%s(): %s", __func__, err_message);
		SNMP_FREE(err_message);
		goto L_error_1;
	}
	sess->version = version;
	free(sess->community);
	sess->community = (u_char *)strdup(community);
	if (!sess->community) {
		emsg_add("%s(): strdup(3) failure.", __func__);
		goto L_error_1;
	}
	sess->community_len = strlen(community);

	obj = Tcl_NewObj();
	if (!obj) {
		emsg_add("%s(): Tcl_NewObj() failed.", __func__);
		goto L_error_1;
	}

	Tcl_InvalidateStringRep(obj);
	TCLOBJ_NETSNMPSESSION_PTR(*obj) = sess;
        obj->typePtr = &tclNetsnmpSessionType;
#ifdef WITH_DEBUGLOG
        DLOG("%20s(): obj = <%p, %s, %d, <%p>>, bytes = %s, length = %d\n", 
		__func__, obj, obj->typePtr ? obj->typePtr->name : "", obj->refCount, 
			TCLOBJ_NETSNMPSESSION_PTR(*obj), obj->bytes, obj->length);
#endif
	emsg_clear();
	Tcl_SetObjResult(interp, obj);
	return TCL_OK;

L_error_1:
	if (sess)
		snmp_close(sess);
        if (interp)
                Tclemsg_AppendResult(interp);
        emsg_clear();
	return TCL_ERROR;
}


struct tcl_snmpvariable {
	Tcl_Obj *tv_name;
	Tcl_Obj *tv_index;
	Tcl_Obj *tv_type;
	Tcl_Obj *tv_value;
	Tcl_Obj *tv_enum;
};

static
void
tcl_snmpvariable_init(struct tcl_snmpvariable *tv)
{
	tv->tv_name = tv->tv_index = tv->tv_type = tv->tv_value = tv->tv_enum = NULL;
}

static
void
tcl_snmpvariable_fini(struct tcl_snmpvariable *tv)
{
	if (tv->tv_name) {
		Tcl_DecrRefCount(tv->tv_name);
		tv->tv_name = 0;
	}
	if (tv->tv_index) {
		Tcl_DecrRefCount(tv->tv_index);
		tv->tv_index = 0;
	}
	if (tv->tv_type) {
		Tcl_DecrRefCount(tv->tv_type);
		tv->tv_type = 0;
	}
	if (tv->tv_value) {
		Tcl_DecrRefCount(tv->tv_value);
		tv->tv_value = 0;
	}
	if (tv->tv_enum) {
		Tcl_DecrRefCount(tv->tv_enum);
		tv->tv_enum = 0;
	}
}

static
int
tcl_snmpvariable_set(Tcl_Interp *interp, struct tcl_snmpvariable *tv, Tcl_Obj *Index, netsnmp_variable_list *v)
{
	if (Index) {
		int llength;
		if (Tcl_ListObjLength(interp, Index, &llength) != TCL_OK)
			goto L_error_1;
		char name[100];
		snprint_objid(name, sizeof name, v->name, v->name_length - llength);
		Tcl_IncrRefCount(Index);
		tv->tv_index = Index;
		tv->tv_name = tcl_get_string(name);
		if (!tv->tv_name)
			goto L_error_2;
	} else {
		oid *oid_index;
		struct tree *t = oid_find(get_tree_head(), v->name, v->name_length, &oid_index);
		if (!t) {
			char oidstr[100];
			snprint_objid(oidstr, sizeof oidstr, v->name, v->name_length);
			emsg_add("%s(): ", oidstr, ": unknown OID.\n", __func__);
			goto L_error_1;
		}
		char name[100];
		snprint_objid(name, sizeof name, v->name, oid_index - v->name);

		tv->tv_index = Tcl_NewListObj(0, 0);
		if (!tv->tv_index) {
			emsg_add("%s(): Tcl_NewListObj(0, 0) failed.", __func__);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_index);
		for (int i = v->name_length - (oid_index - v->name); i; i--)
			Tcl_ListObjAppendElement(interp, tv->tv_index, Tcl_NewLongObj(*oid_index++));
		tv->tv_name = tcl_get_string(name);
		if (!tv->tv_name)
			goto L_error_2;
	}

	if (v->type == ASN_INTEGER) {
		long val = *(long *)v->val.integer;
		tv->tv_value = Tcl_NewLongObj(val);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewLongObj() failed.", __func__);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("INTEGER");
		if (!tv->tv_type)
			goto L_error_2;
		struct tree *t = get_tree(v->name, v->name_length, get_tree_head());
		if (t)
			for (struct enum_list *e = t->enums; e; e = e->next)
				if (e->value == val) {
					tv->tv_enum = tcl_get_string(e->label);
					if (!tv->tv_enum)
						goto L_error_2;
					break;
				}
	} else if (v->type == ASN_COUNTER) {
		tv->tv_value = Tcl_NewWideIntObj(*(u_long *)v->val.integer);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewWideIntObj() failed.", __func__);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("COUNTER");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_GAUGE) {
		tv->tv_value = Tcl_NewWideIntObj(*(u_long *)v->val.integer);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewWideIntObj() failed.", __func__);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("GAUGE");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_TIMETICKS) {
		tv->tv_value = Tcl_NewWideIntObj(*(u_long *)v->val.integer);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewWideIntObj() failed.", __func__);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("TIMETICKS");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_UINTEGER) {
		u_long val = *(u_long *)v->val.integer;
		tv->tv_value = Tcl_NewWideIntObj(val);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewWideIntObj() failed.", __func__);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("UINTEGER");
		if (!tv->tv_type)
			goto L_error_2;
		struct tree *t = get_tree(v->name, v->name_length, get_tree_head());
		if (t)
			for (struct enum_list *e = t->enums; e; e = e->next)
				if (e->value == val) {
					tv->tv_enum = tcl_get_string(e->label);
					if (!tv->tv_enum)
						goto L_error_2;
					break;
				}
	} else if (v->type == ASN_COUNTER64) {
		U64 *u64 = (U64 *)v->val.integer;
		union {
			uint64_t u64;
			uint32_t u32[2];
		} x;
		x.u32[0] = u64->low;
		x.u32[1] = u64->high;
		tv->tv_value = Tcl_NewWideIntObj(x.u64);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewWideIntObj() failed.", __func__);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("COUNTER64");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_OCTET_STR) {
		int hexString = 0;
		const char *typecstr = "STRING";
		struct tree *t;
		const char *descriptor;

		t = get_tree(v->name, v->name_length, get_tree_head());
		descriptor = 0;
		if (t && t->tc_index != -1)
			descriptor = get_tc_descriptor(t->tc_index);
		do {
			if (descriptor) {
				if (!strcmp(descriptor, "DisplayString")) {
					tv->tv_value = Tcl_NewStringObj((char *)v->val.string, v->val_len);
					break;
				}
				/* textual conventions */
			}
			for (int i = 0; i < v->val_len; i++)
				if (!isprint(v->val.string[i]) && !isspace(v->val.string[i])) {
					hexString = 1;
					typecstr = "Hex-STRING";
					break;
				}
			if (hexString) {
				char buf[v->val_len * 2 + 1];
				for (int i = 0; i < v->val_len; i++) {
					uint8_t b = v->val.string[i];
					buf[2*i] = "0123456789abcdef"[(b>>4) & 0x0f];
					buf[2*i + 1] = "0123456789abcdef"[b & 0x0f];
				}
				buf[v->val_len * 2] = 0;
				tv->tv_value = Tcl_NewStringObj(buf, v->val_len * 2);
			} else
				tv->tv_value = Tcl_NewStringObj((char *)v->val.string, v->val_len);
		} while (0);

		if (!tv->tv_value)
			goto L_error_2;
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string(typecstr);
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_IPADDRESS) {
		char ip_str[16];
		uint8_t *ip = (uint8_t *)v->val.string;
		snprintf(ip_str, sizeof ip_str, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
		tv->tv_value = Tcl_NewStringObj(ip_str, -1);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewStringObj(\"%s\", -1) failed.", __func__, ip_str);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("IPADDRESS");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_OPAQUE) {
		tv->tv_value = Tcl_NewStringObj((char *)v->val.string, v->val_len);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewStringObj(\"%s\", %d) failed.", 
				__func__, (char *)v->val.string, v->val_len);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("OPAQUE");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_NSAP) {
		tv->tv_value = Tcl_NewStringObj((char *)v->val.string, v->val_len);
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewStringObj(\"%s\", %d) failed.", 
				__func__, (char *)v->val.string, v->val_len);
			goto L_error_2;
		}
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("NSAP");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_OBJECT_ID) {
		Tcl_DString ds[1];
		char buf[0x40];

		Tcl_DStringInit(ds);
		sprintf(buf, "%" PRIu32, (uint32_t)v->val.objid[0]);
		Tcl_DStringAppend(ds, buf, -1);
		for (int i = 1; i < v->val_len/sizeof(oid); i++) {
			sprintf(buf, ".%" PRIu32, (uint32_t)v->val.objid[i]);
			Tcl_DStringAppend(ds, buf, -1);
		}
		tv->tv_value = Tcl_NewStringObj(Tcl_DStringValue(ds), Tcl_DStringLength(ds));
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewStringObj(\"%s\", %d) failed.", 
				__func__, Tcl_DStringValue(ds), Tcl_DStringLength(ds));
			Tcl_DStringFree(ds);
			goto L_error_2;
		}
		Tcl_DStringFree(ds);
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("OID");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_BIT_STR) {
		Tcl_DString ds[1];
		Tcl_DStringInit(ds);
		for (int i = 0; i < v->val_len; i++) {
			uint8_t b = *(u_char *)(v->val.string + i);
			for (int j = 0; j < 8; j++)
				Tcl_DStringAppend(ds, (b & (128 >> j)) ? "1" : "0", 1);
		}
		tv->tv_value = Tcl_NewStringObj(Tcl_DStringValue(ds), Tcl_DStringLength(ds));
		if (!tv->tv_value) {
			emsg_add("%s(): Tcl_NewStringObj(\"%s\", %d) failed.", 
				__func__, Tcl_DStringValue(ds), Tcl_DStringLength(ds));
			Tcl_DStringFree(ds);
			goto L_error_2;
		}
		Tcl_DStringFree(ds);
		Tcl_IncrRefCount(tv->tv_value);
		tv->tv_type = tcl_get_string("BIT_STR");
		if (!tv->tv_type)
			goto L_error_2;
	} else if (v->type == ASN_NULL) {
		tv->tv_value = tcl_get_string("Null");
		if (!tv->tv_value)
			goto L_error_2;
		tv->tv_type = tcl_get_string("NULL");
		if (!tv->tv_type)
			goto L_error_2;
	} else {
		emsg_add("%s(): Bad type returned: %lu.\n", __func__, (u_long)v->type);
		goto L_error_2;
	}
	return TCL_OK;

L_error_2:
	tcl_snmpvariable_fini(tv);
L_error_1:
	return TCL_ERROR;
}

static
int
tcl_snmpvariable_add_to_dict(Tcl_Interp *interp, Tcl_Obj *d, struct tcl_snmpvariable *tv)
{
	int rc;
	Tcl_Obj *keyv[3];
	keyv[0] = tv->tv_index;
	keyv[1] = tv->tv_name;

	keyv[2] = tcl_get_string("name");
	rc = Tcl_DictObjPutKeyList(interp, d, 3, keyv, tv->tv_name);
	Tcl_DecrRefCount(keyv[2]);
	if (rc != TCL_OK)
		return TCL_ERROR;

	keyv[2] = tcl_get_string("index");
	rc = Tcl_DictObjPutKeyList(interp, d, 3, keyv, tv->tv_index);
	Tcl_DecrRefCount(keyv[2]);
	if (rc != TCL_OK)
		return TCL_ERROR;

	keyv[2] = tcl_get_string("type");
	rc = Tcl_DictObjPutKeyList(interp, d, 3, keyv, tv->tv_type);
	Tcl_DecrRefCount(keyv[2]);
	if (rc != TCL_OK)
		return TCL_ERROR;

	keyv[2] = tcl_get_string("value");
	rc = Tcl_DictObjPutKeyList(interp, d, 3, keyv, tv->tv_value);
	Tcl_DecrRefCount(keyv[2]);
	if (rc != TCL_OK)
		return TCL_ERROR;

	if (tv->tv_enum) {
		keyv[2] = tcl_get_string("enum");
		rc = Tcl_DictObjPutKeyList(interp, d, 3, keyv, tv->tv_enum);
		Tcl_DecrRefCount(keyv[2]);
		if (rc != TCL_OK)
			return TCL_ERROR;
	}
	return TCL_OK;
}

static
Tcl_Obj *
tcl_vbtree_to_dict(Tcl_Interp *interp, struct rbtree *vbtree)
{
	struct rbglue *gi, *gv;
	Tcl_Obj *dict, *index = NULL;

	dict = Tcl_NewDictObj();
	if (!dict) {
		emsg_add("%s(): Tcl_NewDictObj() return NULL. ", __func__);
		goto L_error;
	}
        RBTREE_FOREACH(gi, vbtree) {
                struct vbtree_index *idp = (struct vbtree_index *)rbglue_dptr(gi);
		index = Tcl_NewListObj(0, 0);
		if (!index) {
			emsg_add("%s(): Tcl_NewListObj(0, 0) failed.", __func__);
			goto L_error;
		}
		Tcl_IncrRefCount(index);
		for (int i = 0; i < idp->oid_len; i++)
			if (Tcl_ListObjAppendElement(interp, index, Tcl_NewLongObj(idp->oid[i])) != TCL_OK) {
				emsg_add("%s(): Tcl_ListObjAppendElement(0, 0) failed.", __func__);
				goto L_error;
			}
                RBTREE_FOREACH(gv, idp->vbtree) {
                        netsnmp_variable_list *v = (netsnmp_variable_list *)rbglue_dptr(gv);
			struct tcl_snmpvariable tv[1];
			tcl_snmpvariable_init(tv);
			if (tcl_snmpvariable_set(interp, tv, index, v) != TCL_OK)
				goto L_error;
			int rc = tcl_snmpvariable_add_to_dict(interp, dict, tv);
			tcl_snmpvariable_fini(tv);
			if (rc != TCL_OK)
				goto L_error;
                }
		Tcl_DecrRefCount(index);
        }
	return dict;

L_error:
	if (index)
		Tcl_DecrRefCount(index);
	if (dict)
		Tcl_DecrRefCount(dict);
	return NULL;
}


static
int
netsnmp_getObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	emsg_set_barrier();
	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 0, 0, "netsnmp::get <netsnmp_session> <oid_list>");
		goto L_error_1;
	}
	if (objv[1]->typePtr != &tclNetsnmpSessionType) {
		emsg_add("%s(): wrong type of argument <netsnmp_session>: %s.", __func__, 
				objv[1]->typePtr ? objv[1]->typePtr->name : "");
		goto L_error_1;
	}
	do {
		int nvarnames;
		if (Tcl_ListObjLength(interp, objv[2], &nvarnames) != TCL_OK)
			goto L_error_1;

		char *varnames[nvarnames];
		for (int i = 0; i < nvarnames; i++) {
			Tcl_Obj *obj;
			if (Tcl_ListObjIndex(interp, objv[2], i, &obj) != TCL_OK)
				goto L_error_1;
			varnames[i] = Tcl_GetString(obj);
		}

		struct rbtree *result;
		result = snmpget(TCLOBJ_NETSNMPSESSION_PTR(*objv[1]), varnames, nvarnames);
		if (!result)
			goto L_error_1;
		/* vbtree_print(result); */

		Tcl_Obj *d = tcl_vbtree_to_dict(interp, result);
		vbtree_destroy(result);
		if (!d)
			goto L_error_1;

		Tcl_SetObjResult(interp, d);
		emsg_clear();
		return TCL_OK;
	} while (0);

L_error_1:
        if (interp)
                Tclemsg_AppendResult(interp);
	emsg_clear();
	return TCL_ERROR;
}

static
int
netsnmp_walkObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	emsg_set_barrier();
	if (objc != 3) {
		Tcl_WrongNumArgs(interp, 0, 0, "netsnmp::walk <netsnmp_session> <oid>");
		goto L_error_1;
	}
	if (objv[1]->typePtr != &tclNetsnmpSessionType) {
		emsg_add("%s(): wrong type of argument <netsnmp_session>. type <name := %s, bytes = %s, length := %d>.", 
				__func__, objv[1]->typePtr ? objv[1]->typePtr->name : "", objv[1]->bytes, objv[1]->length);
		goto L_error_1;
	}

	struct rbtree *result;
	result = snmpwalk(TCLOBJ_NETSNMPSESSION_PTR(*objv[1]), Tcl_GetString(objv[2]));
	if (!result)
		goto L_error_1;
	/* vbtree_print(result); */

	Tcl_Obj *d = tcl_vbtree_to_dict(interp, result);
	vbtree_destroy(result);
	if (!d)
		goto L_error_1;

	Tcl_SetObjResult(interp, d);
	emsg_clear();
	return TCL_OK;

L_error_1:
        if (interp)
                Tclemsg_AppendResult(interp);
	emsg_clear();
	return TCL_ERROR;
}

static
int
netsnmp_bulkgetObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	emsg_set_barrier();

	static const char usage[] = 
		"netsnmp::bulkget <netsnmp_session> ?-non-repeaters|-n n? ?-max-repetitions|-r n? ?--|-? <oid_list>";
	if (objc < 3) {
		Tcl_WrongNumArgs(interp, 0, 0, usage);
		goto L_error_1;
	}
	if (objv[1]->typePtr != &tclNetsnmpSessionType) {
		emsg_add("%s(): wrong type of argument <netsnmp_session>: %s.", __func__, 
				objv[1]->typePtr ? objv[1]->typePtr->name : "");
		goto L_error_1;
	}
	netsnmp_session *session = TCLOBJ_NETSNMPSESSION_PTR(*objv[1]);

	int non_rep = 0;
	int max_rep = 16;
	do {
		enum { xGET_OPT, xGET_OPT_ARG, xOPT_END, xPARAMETERS } x;
		enum { optNonRepeaters, optMaxRepetitions } optopt;
		int i;

		x = xGET_OPT;
		for (i = 2; i < objc; i++) {
			const char *s = Tcl_GetString(objv[i]);
			if (x == xGET_OPT) {
				if (!strcmp(s, "-non-repeaters") || !strcmp(s, "-n")) {
					optopt = optNonRepeaters;
					x = xGET_OPT_ARG;
				} else if (!strcmp(s, "-max-repetitions") || !strcmp(s, "-r")) {
					optopt = optMaxRepetitions;
					x = xGET_OPT_ARG;
				} else if (*s == '-') {
					if (!s[1] || (s[1] == '-' && !s[2]))
						x = xOPT_END;
					else {
						emsg_add("%s(): Unknown option: \"%s\".", __func__, s);
						goto L_error_1;
					}
				} else {
					x = xPARAMETERS;
					break;
				}
			} else if (x == xGET_OPT_ARG) {
				if (optopt == optNonRepeaters) {
					if (Tcl_GetIntFromObj(interp, objv[i], &non_rep) != TCL_OK)
						goto L_error_1;
				} else if (optopt == optMaxRepetitions) {
					if (Tcl_GetIntFromObj(interp, objv[i], &max_rep) != TCL_OK)
						goto L_error_1;
				}
				x = xGET_OPT;
			} else if (x == xOPT_END) {
				x = xPARAMETERS;
				break;
			}
		}
		objc -= i;
		objv += i;
	} while (0);

	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 0, 0, usage);
		goto L_error_1;
	}

	do {
		int nvarnames;
		if (Tcl_ListObjLength(interp, objv[0], &nvarnames) != TCL_OK)
			goto L_error_1;

		if (nvarnames < non_rep) {
			emsg_add("%s(): insufficient quantity of elements in the list of oids.\n%s", 
				__func__, usage);
			goto L_error_1;
		}

		char *varnames[nvarnames];
		for (int i = 0; i < nvarnames; i++) {
			Tcl_Obj *obj;
			if (Tcl_ListObjIndex(interp, objv[0], i, &obj) != TCL_OK)
				goto L_error_1;
			varnames[i] = Tcl_GetString(obj);
		}

		struct rbtree *result;
		result = snmpbulkget(session, non_rep, max_rep, varnames, nvarnames);
		if (!result)
			goto L_error_1;
		/* vbtree_print(result); */

		Tcl_Obj *d = tcl_vbtree_to_dict(interp, result);
		vbtree_destroy(result);
		if (!d)
			goto L_error_1;

		Tcl_SetObjResult(interp, d);
		emsg_clear();
		return TCL_OK;
	} while (0);

L_error_1:
        if (interp)
                Tclemsg_AppendResult(interp);
	emsg_clear();
	return TCL_ERROR;
}

static
int
netsnmp_bulkwalkObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	emsg_set_barrier();

	static const char usage[] = 
		"netsnmp::bulkwalk <netsnmp_session> ?-non-repeaters|-n n? ?-max-repetitions|-r n? ?--|-? <oid_list>";
	if (objc < 3) {
		Tcl_WrongNumArgs(interp, 0, 0, usage);
		goto L_error_1;
	}
	if (objv[1]->typePtr != &tclNetsnmpSessionType) {
		emsg_add("%s(): wrong type of argument <netsnmp_session>: %s.", __func__, 
				objv[1]->typePtr ? objv[1]->typePtr->name : "");
		goto L_error_1;
	}
	netsnmp_session *session = TCLOBJ_NETSNMPSESSION_PTR(*objv[1]);

	int non_rep = 0;
	int max_rep = 16;
	do {
		enum { xGET_OPT, xGET_OPT_ARG, xOPT_END, xPARAMETERS } x;
		enum { optNonRepeaters, optMaxRepetitions } optopt;
		int i;

		x = xGET_OPT;
		for (i = 2; i < objc; i++) {
			const char *s = Tcl_GetString(objv[i]);
			if (x == xGET_OPT) {
				if (!strcmp(s, "-non-repeaters") || !strcmp(s, "-n")) {
					optopt = optNonRepeaters;
					x = xGET_OPT_ARG;
				} else if (!strcmp(s, "-max-repetitions") || !strcmp(s, "-r")) {
					optopt = optMaxRepetitions;
					x = xGET_OPT_ARG;
				} else if (*s == '-') {
					if (!s[1] || (s[1] == '-' && !s[2]))
						x = xOPT_END;
					else {
						emsg_add("%s(): Unknown option: \"%s\".", __func__, s);
						goto L_error_1;
					}
				} else {
					x = xPARAMETERS;
					break;
				}
			} else if (x == xGET_OPT_ARG) {
				if (optopt == optNonRepeaters) {
					if (Tcl_GetIntFromObj(interp, objv[i], &non_rep) != TCL_OK)
						goto L_error_1;
				} else if (optopt == optMaxRepetitions) {
					if (Tcl_GetIntFromObj(interp, objv[i], &max_rep) != TCL_OK)
						goto L_error_1;
				}
				x = xGET_OPT;
			} else if (x == xOPT_END) {
				x = xPARAMETERS;
				break;
			}
		}
		objc -= i;
		objv += i;
	} while (0);

	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 0, 0, usage);
		goto L_error_1;
	}

	do {
		int nvarnames;
		if (Tcl_ListObjLength(interp, objv[0], &nvarnames) != TCL_OK)
			goto L_error_1;

		if (nvarnames < non_rep) {
			emsg_add("%s(): insufficient quantity of elements in the list of oids.\n%s", 
				__func__, usage);
			goto L_error_1;
		}

		char *varnames[nvarnames];
		for (int i = 0; i < nvarnames; i++) {
			Tcl_Obj *obj;
			if (Tcl_ListObjIndex(interp, objv[0], i, &obj) != TCL_OK)
				goto L_error_1;
			varnames[i] = Tcl_GetString(obj);
		}

		struct rbtree *result;
		result = snmpbulkwalk(session, non_rep, max_rep, varnames, nvarnames);
		if (!result)
			goto L_error_1;
		/* vbtree_print(result); */

		Tcl_Obj *d = tcl_vbtree_to_dict(interp, result);
		vbtree_destroy(result);
		if (!d)
			goto L_error_1;

		Tcl_SetObjResult(interp, d);
		emsg_clear();
		return TCL_OK;
	} while (0);

L_error_1:
        if (interp)
                Tclemsg_AppendResult(interp);
	emsg_clear();
	return TCL_ERROR;
}

static
int
netsnmp_bulkwalk_exObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	emsg_set_barrier();

	static const char usage[] = "netsnmp::bulkwalk_ex <netsnmp_session> ?-non-repeaters|-n n? ?-max-repetitions|-r n? ?--|-? <oid_list>";
	if (objc < 3) {
		Tcl_WrongNumArgs(interp, 0, 0, usage);
		goto L_error_1;
	}
	if (objv[1]->typePtr != &tclNetsnmpSessionType) {
		emsg_add("%s(): wrong type of argument <netsnmp_session>: %s.", __func__, 
				objv[1]->typePtr ? objv[1]->typePtr->name : "");
		goto L_error_1;
	}
	netsnmp_session *session = TCLOBJ_NETSNMPSESSION_PTR(*objv[1]);

	int non_rep = 0;
	int max_rep = 16;
	do {
		enum { xGET_OPT, xGET_OPT_ARG, xOPT_END, xPARAMETERS } x;
		enum { optNonRepeaters, optMaxRepetitions } optopt;
		int i;

		x = xGET_OPT;
		for (i = 2; i < objc; i++) {
			const char *s = Tcl_GetString(objv[i]);
			if (x == xGET_OPT) {
				if (!strcmp(s, "-non-repeaters") || !strcmp(s, "-n")) {
					optopt = optNonRepeaters;
					x = xGET_OPT_ARG;
				} else if (!strcmp(s, "-max-repetitions") || !strcmp(s, "-r")) {
					optopt = optMaxRepetitions;
					x = xGET_OPT_ARG;
				} else if (*s == '-') {
					if (!s[1] || (s[1] == '-' && !s[2]))
						x = xOPT_END;
					else {
						emsg_add("%s(): Unknown option: \"%s\".", __func__, s);
						goto L_error_1;
					}
				} else {
					x = xPARAMETERS;
					break;
				}
			} else if (x == xGET_OPT_ARG) {
				if (optopt == optNonRepeaters) {
					if (Tcl_GetIntFromObj(interp, objv[i], &non_rep) != TCL_OK)
						goto L_error_1;
				} else if (optopt == optMaxRepetitions) {
					if (Tcl_GetIntFromObj(interp, objv[i], &max_rep) != TCL_OK)
						goto L_error_1;
				}
				x = xGET_OPT;
			} else if (x == xOPT_END) {
				x = xPARAMETERS;
				break;
			}
		}
		objc -= i;
		objv += i;
	} while (0);

	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 0, 0, usage);
		goto L_error_1;
	}

	do {
		int nvarnames;
		if (Tcl_ListObjLength(interp, objv[0], &nvarnames) != TCL_OK)
			goto L_error_1;

		if (nvarnames < non_rep) {
			emsg_add("%s(): insufficient quantity of elements in the list of oids.\n%s", __func__, usage);
			goto L_error_1;
		}

		char *varnames[nvarnames];
		for (int i = 0; i < nvarnames; i++) {
			Tcl_Obj *obj;
			if (Tcl_ListObjIndex(interp, objv[0], i, &obj) != TCL_OK)
				goto L_error_1;
			varnames[i] = Tcl_GetString(obj);
		}

		struct rbtree *result;
		result = snmpbulkwalk_ex(session, non_rep, max_rep, varnames, nvarnames);
		if (!result)
			goto L_error_1;
		/* vbtree_print(result); */

		Tcl_Obj *d = tcl_vbtree_to_dict(interp, result);
		vbtree_destroy(result);
		if (!d)
			goto L_error_1;

		Tcl_SetObjResult(interp, d);
		emsg_clear();
		return TCL_OK;
	} while (0);

L_error_1:
        if (interp)
                Tclemsg_AppendResult(interp);
	emsg_clear();
	return TCL_ERROR;
}


static
int
netsnmp_setObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	static const char usage[] = "netsnmp::set <netsnmp_session> <oid> = <val> [oid = val [...]]";

	netsnmp_session *session;
	netsnmp_pdu *pdu, *response;
	int status;

	emsg_set_barrier();

	if (objc < 5 || ((objc-2) % 3) != 0) {
		emsg_add("%s(): %s", __func__, usage);
		goto L_error_1;
	}
	if (objv[1]->typePtr != &tclNetsnmpSessionType) {
		emsg_add("%s(): wrong type of argument <netsnmp_session>: %s.", __func__, 
				objv[1]->typePtr ? objv[1]->typePtr->name : "");
		goto L_error_1;
	}
	session = TCLOBJ_NETSNMPSESSION_PTR(*objv[1]);
	objc -= 2;
	objv += 2;

	pdu = snmp_pdu_create(SNMP_MSG_SET);
	if (!pdu) {
		emsg_add("%s(): snmp_pdu_create(SNMP_MSG_SET): %s.", __func__, snmp_api_errstring(snmp_errno));
		goto L_error_1;
	}

	do {
		char *name, type, *value;
		int namelen;
		oid oid[MAX_OID_LEN];
		size_t oidlen = MAX_OID_LEN;

		int failures = 0;
		for (int i = 0; i < objc; i += 3) {
			name = Tcl_GetStringFromObj(objv[i], &namelen);
			oidlen = MAX_OID_LEN;
			if (!snmp_parse_oid(name, oid, &oidlen)) {
				emsg_add("%s(): %s: %s.", __func__, name, snmp_api_errstring(snmp_errno));
				failures++;
				continue;
			}
			type = *Tcl_GetString(objv[i + 1]);
			value = Tcl_GetString(objv[i + 2]);
			if (snmp_add_var(pdu, oid, oidlen, type, value)) {
				emsg_add("%s(): Unable add variable \"%s\" \"%c\" \"%s\" to PDU: %s.",
					__func__, name, type, value, snmp_api_errstring(snmp_errno));
				failures++;
				continue;
			}
		}
		if (failures) {
			snmp_free_pdu(pdu);
			goto L_error_1;
		}
	} while (0);

	response = 0;
	status = snmp_synch_response(session, pdu, &response);
	if (status == STAT_SUCCESS) {
		int errstat = response->errstat;
		if (errstat == SNMP_ERR_NOERROR) {
			snmp_free_pdu(response);
			emsg_clear();
			return TCL_OK;
		}
		FILE *fp = emsg_fopen();
		fprintf(fp, "%s(): Error in packet: \"%s\".\n", __func__, snmp_errstring(errstat));
		if (response->errindex) {
			fprintf(fp, "\tFailed object: ");
			int i = 0;
			for (netsnmp_variable_list *v = response->variables; v; v = v->next_variable)
				if (++i == response->errindex)
					fprint_objid(fp, v->name, v->name_length);
		}
		fclose(fp);
		snmp_free_pdu(response);
	} else if (status == STAT_TIMEOUT) {
		emsg_add("%s(): Timeout: No Response from %s.", __func__, session->peername);
	} else {
                char *errstr;
                snmp_error(session, 0, 0, &errstr);
                emsg_add("%s(): %s", __func__, errstr);
                free(errstr);
	}
L_error_1:
        if (interp)
                Tclemsg_AppendResult(interp);
	emsg_clear();
	return TCL_ERROR;
}

static
int
netsnmp_add_mibdirObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	char *dir_name;

	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "directory");
		return TCL_ERROR;
	}
	dir_name = Tcl_GetStringFromObj(objv[1], 0);
	if (!dir_name)
		return TCL_ERROR;
	add_mibdir(dir_name);
	return TCL_OK;
}

#if !defined(NO_NETSNMP_READ_MODULE)
static
int
netsnmp_read_moduleObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	char *module;

	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "module-name");
		return TCL_ERROR;
	}
	module = Tcl_GetStringFromObj(objv[1], 0);
	if (!module)
		return TCL_ERROR;
	netsnmp_read_module(module);	/* XXX what about errors? */
	return TCL_OK;
}
#endif

static
int
netsnmp_read_mibObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	char *mib;

	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "mib-file");
		return TCL_ERROR;
	}
	mib = Tcl_GetStringFromObj(objv[1], 0);
	if (!mib)
		return TCL_ERROR;
	read_mib(mib);
	return TCL_OK;
}

static
int
netsnmp_read_all_mibsObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	char *mib;

	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 1, objv, "<no args>");
		return TCL_ERROR;
	}
	read_all_mibs();
	return TCL_OK;
}

static
int
netsnmp_oidObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "name");
		return TCL_ERROR;
	}

	oid oidbuf[MAX_OID_LEN];
	size_t oidlen = MAX_OID_LEN;
	char const *name = Tcl_GetString(objv[1]);

	if (!read_objid(name, oidbuf, &oidlen)) {
		Tcl_AppendResult(interp, name, ": ", snmp_api_errstring(snmp_errno), NULL);
		return TCL_ERROR;
	}

	Tcl_Obj *r = Tcl_ObjPrintf("%" NETSNMP_PRIo "u", oidbuf[0]);
	if (!r)
		return TCL_ERROR;
	Tcl_IncrRefCount(r);

	for (size_t i = 1; i < oidlen; i++)
		Tcl_AppendPrintfToObj(r, ".%" NETSNMP_PRIo "u", oidbuf[i]);

	Tcl_SetObjResult(interp, r);
	Tcl_DecrRefCount(r);
	return TCL_OK;
}

static
int
netsnmp_nameObjCmd(ClientData clientData __unused, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]) 
{
	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "Oid");
		return TCL_ERROR;
	}

	oid oidbuf[MAX_OID_LEN];
	size_t oidlen = MAX_OID_LEN;
	if (!snmp_parse_oid(Tcl_GetString(objv[1]), oidbuf, &oidlen)) {
		Tcl_AppendResult(interp, Tcl_GetString(objv[1]), ": ", snmp_api_errstring(snmp_errno), ".\n", NULL);
		return TCL_ERROR;
	}
	char name[512];
	snprint_objid(name, sizeof name, oidbuf, oidlen);
	Tcl_SetResult(interp, name, TCL_VOLATILE);
	return TCL_OK;
}

int PACKAGE_INIT(Tcl_Interp *interp)
{
	if (!Tcl_InitStubs(interp, __XSTRING(TCLVERSION), 0))
		return TCL_ERROR;

	Tcl_CreateObjCommand(interp, "netsnmp::session", netsnmp_sessionObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::get", netsnmp_getObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::walk", netsnmp_walkObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::bulkget", netsnmp_bulkgetObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::bulkwalk", netsnmp_bulkwalkObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::bulkwalk_ex", netsnmp_bulkwalk_exObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::set", netsnmp_setObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::add_mibdir", netsnmp_add_mibdirObjCmd, 0, 0);
#if !defined(NO_NETSNMP_READ_MODULE)
	Tcl_CreateObjCommand(interp, "netsnmp::read_module", netsnmp_read_moduleObjCmd, 0, 0);
#endif
	Tcl_CreateObjCommand(interp, "netsnmp::read_mib", netsnmp_read_mibObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::read_all_mibs", netsnmp_read_all_mibsObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::oid", netsnmp_oidObjCmd, 0, 0);
	Tcl_CreateObjCommand(interp, "netsnmp::name", netsnmp_nameObjCmd, 0, 0);

	Tcl_PkgProvide(interp, __XSTRING(PACKAGE), __XSTRING(PACKAGE_VERSION));

	return TCL_OK;
}

