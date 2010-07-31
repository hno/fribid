/*

  Copyright (c) 2009 Samuel Lidén Borell <samuel@slbdata.se>
 
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

#define _BSD_SOURCE 1

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <libp11.h>
#include <openssl/x509.h>
#include <sechash.h>

#include "keyfile.h"
#include "xmldsig.h"
#include "misc.h"

static const char xmldsig_template[] = 
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>"
    "<Signature xmlns=\"http://www.w3.org/2000/09/xmldsig#\">"
        "%s"
        "<SignatureValue>%s</SignatureValue>"
        "%s"
        "<Object>%s</Object>"
    "</Signature>";

static const char signedinfo_template[] =
    "<SignedInfo xmlns=\"http://www.w3.org/2000/09/xmldsig#\">"
        "<CanonicalizationMethod Algorithm=\"http://www.w3.org/TR/2001/REC-xml-c14n-20010315\">"
        "</CanonicalizationMethod>"
        "<SignatureMethod Algorithm=\"http://www.w3.org/2000/09/xmldsig#rsa-sha1\">"
        "</SignatureMethod>"
        "<Reference Type=\"http://www.bankid.com/signature/v1.0.0/types\" URI=\"#bidSignedData\">"
            "<Transforms>"
                "<Transform Algorithm=\"http://www.w3.org/TR/2001/REC-xml-c14n-20010315\"></Transform>"
            "</Transforms>"
            "<DigestMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#sha256\"></DigestMethod>"
            "<DigestValue>%s</DigestValue>"
        "</Reference>"
        "<Reference URI=\"#bidKeyInfo\">"
            "<Transforms>"
                "<Transform Algorithm=\"http://www.w3.org/TR/2001/REC-xml-c14n-20010315\"></Transform>"
            "</Transforms>"
            "<DigestMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#sha256\"></DigestMethod>"
            "<DigestValue>%s</DigestValue>"
        "</Reference>"
    "</SignedInfo>";


static const char keyinfo_template[] =
    "<KeyInfo xmlns=\"http://www.w3.org/2000/09/xmldsig#\" Id=\"bidKeyInfo\">"
        "<X509Data>%s</X509Data>"
    "</KeyInfo>";

static const char cert_template[] =
    "<X509Certificate>%s</X509Certificate>";
#define MAX_SIGSIZE 256

/**
 * Creates a xmldsig signature. See the sign function in bankid.c.
 */
char *xmldsig_sign(PKCS11_SLOT *slot,
                   const char *p12Data, const int p12Length,
                   const KeyfileSubject *person,
                   const unsigned int certMask,
                   const char *password,
                   const char *dataId, const char *data) {
    
    bool smartcard = slot != NULL;
    PKCS11_CERT *authcert;
    int rc;

    if (smartcard) {
        unsigned int ncerts;
    	PKCS11_CERT *certs;

        /* get all certs */
        rc = PKCS11_enumerate_certs(slot->token, &certs, &ncerts);
        if (rc) {
            fprintf(stderr, "PKCS11_enumerate_certs failed\n");
            return NULL;
        }
        if (ncerts <= 0) {
            fprintf(stderr, "no certificates found\n");
            return NULL;
        }

        /* use the first cert */
        authcert=&certs[0];
    }

    // Keyinfo
    char **certs;
    int certCount;

    if (smartcard) {
        if (!smartcard_getBase64Chain(slot, &certs, &certCount)) {
            fprintf(stderr, "smartcard base64 failed..\n");
            return NULL;
        }
    } else {
        if (!keyfile_getBase64Chain(p12Data, p12Length, person, certMask,
                                    &certs, &certCount)) {
            return NULL;
        }      
    }

    int certsLength = (strlen(cert_template)-2) * certCount;
    for (int i = 0; i < certCount; i++) {
        certsLength += strlen(certs[i]);
    }

    char *keyinfoInner = malloc(certsLength+1);
    keyinfoInner[0] = '\0';
    char *keyend = keyinfoInner;
    for (int i = 0; i < certCount; i++) {
        keyend += sprintf(keyend, cert_template, certs[i]);
        free(certs[i]);
    }
    free(certs);
    
    char *keyinfo = rasprintf(keyinfo_template, keyinfoInner);
    free(keyinfoInner);
    
    // SignedInfo
    char *data_sha = sha_base64(data);
    char *keyinfo_sha = sha_base64(keyinfo);
    
    char *signedinfo = rasprintf(signedinfo_template, data_sha, keyinfo_sha);
    free(keyinfo_sha);
    free(data_sha);
    
    
    // Signature
    char *sigData;
    int sigLen;
    if (smartcard) {
        PKCS11_KEY *authkey;
        char shasum[SHA1_LENGTH];

        rc = PKCS11_login(slot, 0, NULL	);
        if (rc != 0) {
            return NULL;
        }

        authkey = PKCS11_find_key(authcert);
        if (!authkey) {
            return NULL;
        }

        sigLen = MAX_SIGSIZE;
        sigData = malloc(MAX_SIGSIZE);

        //TODO: flytta till misc.c
        HASH_HashBuf(HASH_AlgSHA1, (unsigned char*)shasum, (unsigned char*)signedinfo, strlen(signedinfo));

	    rc = PKCS11_sign(NID_sha1, (const unsigned char*)shasum, SHA1_LENGTH, (unsigned char*)sigData, (unsigned int*) &sigLen, authkey);
	    if (rc != 1) {
            return NULL;
	    }
    } else {
        if (!keyfile_sign(p12Data, p12Length, person, certMask, password,
                          signedinfo, strlen(signedinfo), &sigData, &sigLen)) {
            free(keyinfo);
            free(signedinfo);
            return NULL;
        }
    }
    char *signature = base64_encode(sigData, sigLen);
    free(sigData);
    
    // Glue everything together
    char *complete = rasprintf(xmldsig_template,
                               signedinfo, signature, keyinfo, data);
    
    free(keyinfo);
    free(signedinfo);
    free(signature);
    
    return complete;
}

