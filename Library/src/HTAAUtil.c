/*
**	COMMON PARTS OF ACCESS AUTHORIZATION MODULE
**	FOR BOTH SERVER AND BROWSER
**
**	(c) COPYRIGHT MIT 1995.
**	Please first read the full copyright statement in the file COPYRIGH.
**	@(#) $Id$
**
**	The authentication information is stored in a list of authentication
**	data bases, each uniquely identified by a hostname and a port number.
**	Each data base contains a set of templates which can be used to predict
**	what information to use in a hierarchical tree. All authentication
**	dependent information is stored as opaque data in a anode. Normally
**	a server application would only keep one auth base but if it wants
**	different protection setup as a function of different interfaces then
**	it can have one auth base representing each interface. For example a
**	server with interfaces "www.foo.com" and "internal.foo.com" can have
**	different protection setups for each interface.
**
** AUTHORS:
**	AL	Ari Luotonen	luotonen@dxcern.cern.ch
**	MD 	Mark Donszelmann    duns@vxdeop.cern.ch
**	HFN	Henrik Frystyk
**
** HISTORY:
**	 8 Nov 93  MD	(VMS only) Added case insensitive comparison
**			in HTAA_templateCaseMatch
*/

/* Library include files */
#include "sysdep.h"
#include "WWWUtil.h"
#include "WWWCore.h"
#include "HTAAUtil.h"					 /* Implemented here */

#define AA_NAME			"w3c-AA"	      /* Name of the AA tree */
#define DEFAULT_PORT            80                    /* Concentrate on HTTP */

struct _HTAAModule {
    char *		scheme;
    HTNetCallback *	before;
    HTNetCallback *	after;
    HTUTree_gc *	gc;
};

typedef struct _HTAAElement {
    char *		scheme;
    void * 		context;
} HTAAElement;

PRIVATE HTList * HTSchemes;	/* List of registered authentication schemes */

/* ------------------------------------------------------------------------- */
/*			 AUTHENTICATION MODULE MANAGEMENT		     */
/* ------------------------------------------------------------------------- */

PRIVATE BOOL delete_module (HTAAModule * module)
{
    if (module) {
	HT_FREE(module->scheme);
	HT_FREE(module);
	return YES;
    }
    return NO;
}

PRIVATE HTAAModule * find_module (const char * scheme)
{
    if (!HTSchemes) HTSchemes = HTList_new();
    if (scheme) {
	HTList * cur = HTSchemes;
	HTAAModule * pres = NULL;
	while ((pres = (HTAAModule *) HTList_nextObject(cur)))
	    if (!strcasecomp(pres->scheme, scheme)) return pres;
    } else
	if (AUTH_TRACE) HTTrace("Auth Engine. Bad argument\n");
    return NULL;
}

PUBLIC HTAAModule * HTAA_newModule (const char *	scheme,
				    HTNetCallback *	before,
				    HTNetCallback *	after,
				    HTUTree_gc *	gc)
{
    if (scheme) {
	HTAAModule * pres = find_module(scheme);

	/* If found then update entry - else create a new one */
	if (!pres) {
	    if (!(pres = (HTAAModule *) HT_CALLOC(1, sizeof(HTAAModule))))
		HT_OUTOFMEM("HTAA_newModule");
	    StrAllocCopy(pres->scheme, scheme);
	    pres->before = before;
	    pres->after = after;
	    pres->gc = gc;

	    /* Add the new AA Module to the list */
	    HTList_addObject(HTSchemes, (void *) pres);
	    if (AUTH_TRACE) HTTrace("Auth Engine. Created module %p\n", pres);
	} else {
	    if (AUTH_TRACE) HTTrace("Auth Engine. Found module %p\n", pres);
	}
	return pres;
    } else {
	if (AUTH_TRACE) HTTrace("Auth Engine. Bad argument\n");
	return NULL;
    }
}

PUBLIC HTAAModule * HTAA_findModule (const char * scheme)
{
    if (scheme) {
	HTAAModule * pres = find_module(scheme);
	if (AUTH_TRACE)
	    HTTrace("Auth Engine. did %sfind %s\n", pres ? "" : "NOT ",scheme);
	return pres;
    } else {
	if (AUTH_TRACE) HTTrace("Auth Engine. Bad augument\n");
    }
    return NULL;
}

PUBLIC BOOL HTAA_deleteModule (const char * scheme)
{
    if (scheme) {
	HTAAModule * pres = find_module(scheme);
	if (pres) {
	    HTList_removeObject(HTSchemes, pres);
	    if (AUTH_TRACE) HTTrace("Auth Engine. deleted %p\n", pres);
	    delete_module(pres);
	    return YES;
	}
    }
    return NO;
}

PUBLIC BOOL HTAA_deleteAllModules (void)
{
    if (HTSchemes) {
	HTList * cur = HTSchemes;
	HTAAModule * pres;
	while ((pres = (HTAAModule *) HTList_nextObject(cur)))
	    delete_module(pres);
	HTList_delete(HTSchemes);
	HTSchemes = NULL;
	return YES;
    }
    return NO;
}

/* ------------------------------------------------------------------------- */
/*			    HANDLE THE AA URL TREE			     */
/* ------------------------------------------------------------------------- */

/*
**	A AA element is a particular AA procotol associated with a
**	particular point in the URL tree. The scheme is the name of the
**	AA protocol known to be able to handle this context. This protocol
**	must have been registered as a AA module.
*/
PRIVATE HTAAElement * HTAA_newElement (const char * scheme, void * context)
{
    if (scheme) {
	HTAAElement * me;
	if ((me = (HTAAElement *) HT_CALLOC(1, sizeof(HTAAElement))) == NULL)
	    HT_OUTOFMEM("HTAAElement_new");
	StrAllocCopy(me->scheme, scheme);
	me->context = context;
	if (AUTH_TRACE) HTTrace("Auth Engine. Created element %p\n", me);
	return me;
    }
    return NULL;
}

/*
**	If the new scheme differs from the existing one then use the
**	new context, otherwise only override the old context if new
**	one differs from NULL
*/
PRIVATE BOOL HTAA_updateElement (HTAAElement * element,
				 const char * scheme, void * context)
{
    if (element && scheme) {
	/*
	** If the old scheme differs from the new one then delete our
	** context by calling the gc provided by the caller
	*/
	if (strcmp(element->scheme, scheme)) {
	    HTAAModule * module = HTAA_findModule(element->scheme);
	    if (module && module->gc && element->context)
		(*module->gc)(element->context);
	    /*
	    **  Insert the new scheme
	    */
	    StrAllocCopy(element->scheme, scheme);
	    element->context = context;
	} else if (context && context != element->context)
	    element->context = context;
	return YES;
    }
    return NO;
}

PRIVATE int HTAA_deleteElement (void * context)
{
    HTAAElement * me = (HTAAElement *) context;
    if (me) {
	HTAAModule * module = HTAA_findModule(me->scheme);

	/* If module then call the gc of the Authentication Module */
	if (module && module->gc && me->context)
	    (*module->gc)(me->context);

	if (AUTH_TRACE) HTTrace("Auth Engine. Deleted element %p\n", me);
	HT_FREE(me->scheme);
	HT_FREE(me);
	return YES;
    }
    return NO;
}

/*
**	Find AA Element
**	---------------
**	Seaches the set of authentication information bases for a match
**	In order to find an anode we do the following:
**
**		1) Find the right auth base
**		2) See if there is a realm match
**		3) See if there is a template match for URL
**
**	Return the node found else NULL which means that we don't have any
**	authentication information to hook on to this request or response
*/
PRIVATE HTAAElement * HTAA_findElement (const char * realm, const char * url)
{
    HTUTree * tree;
    if (!url) {
	if (AUTH_TRACE) HTTrace("Auth Engine. Bad argument\n");
	return NULL;
    }
    if (AUTH_TRACE) HTTrace("Auth Engine. Looking up `%s'\n", url);

    /* Find an existing URL Tree for this URL (if any) */
    {
	char * host = HTParse(url, "", PARSE_HOST);
	char * colon = strchr(host, ':');
	int port = DEFAULT_PORT;
	if (colon ) {
	    *(colon++) = '\0';			     /* Chop off port number */
	    port = atoi(colon);
	}	
	tree = HTUTree_find(AA_NAME, host, port);
	HT_FREE(host);
	if (!tree) {
	    if (AUTH_TRACE) HTTrace("Auth Engine. No information\n");
	    return NULL;
	}
    }

    /* Find a matching AA element (if any) */
    {
	char * path = HTParse(url, "", PARSE_PATH);
	HTAAElement *element = (HTAAElement*)HTUTree_findNode(tree,realm,path);
	HT_FREE(path);
	return element;
    }
    return NULL;
}

/*	Add a AA context to the URL tree
**	--------------------------------
**	Each node in the AA URL tree is a list of the modules we must call
**	for this particular node.
*/
PUBLIC void * HTAA_updateNode (char const * scheme,
			       const char * realm, const char * url,
			       void * context)
{
    HTUTree * tree = NULL;
    HTAAModule * module = NULL;
    if (!scheme || !url) {
	if (AUTH_TRACE) HTTrace("Auth Engine. Bad argument\n");
	return NULL;
    }
    if (AUTH_TRACE) HTTrace("Auth Engine. Adding info for `%s'\n", url);

    /* Find the AA module with this name */
    if ((module = HTAA_findModule(scheme)) == NULL) {
	if (AUTH_TRACE) HTTrace("Auth Engine. Module `%s\' not registered\n",
			       scheme ? scheme : "<null>");
	return NULL;
    }

    /* Find an existing URL Tree or create a new one */
    {
	char * host = HTParse(url, "", PARSE_HOST);
	char * colon = strchr(host, ':');
	int port = DEFAULT_PORT;
	if (colon ) {
	    *(colon++) = '\0';			     /* Chop off port number */
	    port = atoi(colon);
	}
	tree = HTUTree_new(AA_NAME, host, port, HTAA_deleteElement);
	HT_FREE(host);
	if (!tree) {
	    if (AUTH_TRACE) HTTrace("Auth Engine. Can't create tree\n");
	    return NULL;
	}
    }

    /* Find a matching AA element or create a new one */
    {
	char * path = HTParse(url, "", PARSE_PATH);
	HTAAElement * element = NULL;
	BOOL status;
	if ((element = (HTAAElement *) HTUTree_findNode(tree, realm, path)))
	    status = HTAA_updateElement(element, scheme, context);
	else {
	    element = HTAA_newElement(scheme, context);
	    status = HTUTree_addNode(tree, realm, path, element);
	}
	HT_FREE(path);
	return status==YES ? element->context : NULL;
    }
}

/* ------------------------------------------------------------------------- */
/*			       AUTHENTICATION ENGINE 			     */
/* ------------------------------------------------------------------------- */

/*	HTAA_beforeFilter
**	------------------
**	Make a lookup in the URL tree to find any context for this node,
**	If no context is found then we assume that we don't know anything about
**	this URL and hence we don't call any BEFORE filters at all.
**	Return HT_OK or whatever callback returns
*/
PUBLIC int HTAA_beforeFilter (HTRequest * request, void * param, int status)
{
    char * url = HTAnchor_physical(HTRequest_anchor(request));
    const char * realm = HTRequest_realm(request);
    HTAAElement * element = HTAA_findElement(realm, url); 

    /* Delete any old challenges if any */
    if (element) {
	HTAAModule * module = HTAA_findModule(element->scheme);
	if (module) {
	    HTRequest_deleteChallenge(request);
	    if (AUTH_TRACE) HTTrace("Auth Engine. Found BEFORE filter %p\n",
				   module->before);
	    return (*module->before)(request, element->context,status);
	}
    }
    return HT_OK;
}

/*	HTAA_afterFilter
**	-----------------
**	Call the AFTER filter that knows how to handle this scheme.
**	Return YES or whatever callback returns
*/
PUBLIC BOOL HTAA_afterFilter (HTRequest * request, void * param, int status)
{
    const char * scheme = HTRequest_scheme(request);
    HTAAModule * module = NULL;
    /*
    **	If we don't have a scheme then the server has made an error. We
    **  try to make up for it by creating our own "noop" realm and use basic.
    */
    if (!scheme) {
	HTRequest_addChallenge(request, "basic", "realm UNKNOWN");
	scheme = "basic";
    }
    if ((module = HTAA_findModule(scheme)) != NULL) {
	if (AUTH_TRACE)
	    HTTrace("Auth Engine. Found AFTER filter %p\n", module->after);
	HTRequest_deleteCredentials(request);
	return (*module->after)(request, NULL, status);
    }
    return HT_ERROR;
}
