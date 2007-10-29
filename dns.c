/****************************************************************\

Copyright 2004 Enzo Michelangeli

This file is part of the KadC library.

KadC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

KadC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with KadC; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

In addition, closed-source licenses for this software may be granted
by the copyright owner on commercial basis, with conditions negotiated
case by case. Interested parties may contact Enzo Michelangeli at one
of the following e-mail addresses (replace "(at)" with "@"):

 em(at)em.no-ip.com
 em(at)i-t-vision.com

\****************************************************************/

#include <string.h>
#include <assert.h>

#include <KadCalloc.h>

#include <dns.h>

#define arraysize(a) (sizeof(a)/sizeof(*(a)))

static dns_parse_status dns_question_parse(dns_question *pdnsrr, char **ppb, char *buf, char *bufend);
static dns_parse_status dns_rr_parse(dns_rr *pdnsrr, char **ppb, char *buf, char *bufend);

/* converts a DNS message into a newly-allocated dns_msg structure
   return the structure dns_msg, or NULL in case of errors */
dns_msg *dns_parse(char *buf, int buflen) {
	dns_msg *dp;
	unsigned char *ubuf = (unsigned char *)buf;
	dns_parse_status status;
	int i;

	if(buflen < 12)	/* DNS header size and minimum payload */
		return NULL;
	dp = calloc(1, sizeof(dns_msg));
	if(dp == NULL)
		return NULL;
	dp->id =     (ubuf[0] << 8) + ubuf[1];

	dp->qr =     (ubuf[2] >> 7) & 0x01;
	dp->opcode = (ubuf[2] >> 3) & 0x0f;
	dp->aa =     (ubuf[2] >> 2) & 0x01;
	dp->tc =     (ubuf[2] >> 1) & 0x01;
	dp->rd =     (ubuf[2]     ) & 0x01;

	dp->ra =     (ubuf[3] >> 7) & 0x01;
	dp->z =      (ubuf[3] >> 6) & 0x01;
	dp->ad =     (ubuf[3] >> 5) & 0x01;
	dp->cd =     (ubuf[3] >> 4) & 0x01;
	dp->rcode =  (ubuf[3]     ) & 0x0f;

	dp->nquestions = (ubuf[4]  << 8) + ubuf[5];
	dp->nanswer_rr = (ubuf[6]  << 8) + ubuf[7] ;
	dp->nauth_rr =   (ubuf[8]  << 8) + ubuf[9];
	dp->naddit_rr =  (ubuf[10] << 8) + ubuf[11];

	ubuf += 12;	/* point just after the header */

	if(dp->nquestions > 0)	{
		if((dp->questions = calloc(dp->nquestions, sizeof(*(dp->questions)) )) == NULL)
			goto err;
		for(i=0; i<	dp->nquestions; i++) {
			if((dp->questions[i] = calloc(1, sizeof(dns_question))) == NULL)
				goto err;
			status = dns_question_parse(dp->questions[i], (char **)&ubuf, buf, buf+buflen);
			if(status != OK) {
				goto err;
			}
		}
	}
	if(dp->nanswer_rr > 0)	{
		if((dp->answer_rr = calloc(dp->nanswer_rr, sizeof(*(dp->answer_rr)) )) == NULL)
			goto err;
		for(i=0; i<	dp->nanswer_rr; i++) {
			if((dp->answer_rr[i] = calloc(1, sizeof(dns_rr))) == NULL)
				goto err;
			status = dns_rr_parse(dp->answer_rr[i], (char **)&ubuf, buf, buf+buflen);
			if(status != OK) {
				goto err;
			}
		}
	}
	if(dp->nauth_rr > 0)	{
		if((dp->auth_rr = calloc(dp->nauth_rr, sizeof(*(dp->auth_rr)) )) == NULL)
			goto err;
		for(i=0; i<	dp->nauth_rr; i++) {
			if((dp->auth_rr[i] = calloc(1, sizeof(dns_rr))) == NULL)
				goto err;
			status = dns_rr_parse(dp->auth_rr[i], (char **)&ubuf, buf, buf+buflen);
			if(status != OK) {
				goto err;
			}
		}
	}
	if(dp->naddit_rr > 0)	{
		if((dp->addit_rr = calloc(dp->naddit_rr, sizeof(*(dp->addit_rr)) )) == NULL)
			goto err;
		for(i=0; i<	dp->naddit_rr; i++) {
			if((dp->addit_rr[i] = calloc(1, sizeof(dns_rr))) == NULL)
				goto err;
			status = dns_rr_parse(dp->addit_rr[i], (char **)&ubuf, buf, buf+buflen);
			if(status != OK) {
				goto err;
			}
		}
	}

	return dp;

err:
	dns_msg_destroy(dp);
	return NULL;
}

static int dns_question_pack(char **ppb, char *bufend, dns_question *pdnsq);
static int dns_rr_pack(char **ppb, char *bufend, dns_rr *pdnsrr);

int dns_pack(char *buf, int bufsize, dns_msg *dp) {
	char tempbuf[4096];	/* reasonably large */
	char *p;
	int i;
	int availspace = bufsize;

	if(buf==0) {
		buf = tempbuf;
		bufsize = arraysize(tempbuf);
	}

	/* prepare header */
	buf[0] = dp->id >> 8;
	buf[1] = dp->id & 0xff;
	buf[2] = (dp->qr << 7) + (dp->opcode << 3) + (dp->aa << 2) + (dp->tc <<1) + dp->rd;
	buf[3] = (dp->ra << 7) + (dp->z << 6) + (dp->ad << 5) + (dp->cd << 4) + dp->rcode;
	buf[4] = dp->nquestions >> 8;
	buf[5] = dp->nquestions & 0xff;
	buf[6] = dp->nanswer_rr >> 8;
	buf[7] = dp->nanswer_rr & 0xff;
	buf[8] = dp->nauth_rr >> 8;
	buf[9] = dp->nauth_rr & 0xff;
	buf[10] = dp->naddit_rr >> 8;
	buf[11] = dp->naddit_rr & 0xff;

	p = buf+12;
	/* append questions */
	for(i=0; i< dp->nquestions; i++) {
		if(dp->questions != NULL && dp->questions[i] != NULL) {
			dns_question_pack(&p, buf+bufsize, dp->questions[i]);
		}
	}

	/* append answer rr */
	for(i=0; i< dp->nanswer_rr; i++) {
		if(dp->answer_rr != NULL && dp->answer_rr[i] != NULL) {
			dns_rr_pack(&p, buf+bufsize, dp->answer_rr[i]);
		}
	}

	/* append auth rr */
	for(i=0; i< dp->nauth_rr; i++) {
		if(dp->auth_rr != NULL && dp->auth_rr[i] != NULL) {
			dns_rr_pack(&p, buf+bufsize, dp->auth_rr[i]);
		}
	}

	for(i=0; i< dp->naddit_rr; i++) {
		if(dp->addit_rr != NULL && dp->addit_rr[i] != NULL) {
			dns_rr_pack(&p, buf+bufsize, dp->addit_rr[i]);
		}
	}

	if((p - buf) > availspace && availspace > 0)
		return 0;
	else
		return p - buf;
}

static void zfree(void *p) {
	if(p != NULL)
		free(p);
}

void dns_msg_destroy(dns_msg *dp) {
	int i;
	if(dp != NULL) {

		if(dp->nquestions != 0) {
			if(dp->questions != NULL) {
				for(i=0; i < dp->nquestions; i++) {
					if(dp->questions[i] != NULL) {
						zfree(dp->questions[i]->raw);
						free(dp->questions[i]);
					}
				}
				zfree(dp->questions);
			}
		}

		if(dp->nanswer_rr != 0) {
			if(dp->answer_rr != NULL) {
				for(i=0; i < dp->nanswer_rr; i++) {
					zfree(dp->answer_rr[i]->rdata);
					zfree(dp->answer_rr[i]);
				}
				zfree(dp->answer_rr);
			}
		}

		if(dp->nauth_rr != 0) {
			if(dp->auth_rr != NULL) {
				for(i=0; i < dp->nauth_rr; i++) {
					zfree(dp->auth_rr[i]->rdata);
					zfree(dp->auth_rr[i]);
				}
				zfree(dp->auth_rr);
			}
		}

		if(dp->naddit_rr != 0) {
			if(dp->addit_rr != NULL) {
				for(i=0; i < dp->naddit_rr; i++) {
					zfree(dp->addit_rr[i]->rdata);
					zfree(dp->addit_rr[i]);
				}
				zfree(dp->addit_rr);
			}
		}

		free(dp);
	}
}


/* decodes possibly packed name fields. Offsets are relative to buf, which contains the UDP payload. */
static dns_parse_status dns_decompress(char *name, char *nameend, char **ppb, char *buf, char *bufend) {
	unsigned char **uppb = (unsigned char **)ppb;
	if(**uppb == 0) {
		*name = 0;	/* zero-length name, hmmmm... */
		(*uppb)++;
		return OK;
	}
	for(;;) {	/* for all the labels */
		unsigned int c;
		if(name >= nameend) {
			return NAME_TOO_LONG;		/* error! name > 255 chars?? */
		}
		if(*uppb >= (unsigned char *)bufend) {
			return ILL_FORMED_INPUT;	/* error! corrupted packet */
		}
		c = *(*uppb)++;	/* maybe label length or zero or first byte of pointer */
		if(c == 0) {				/* it's a terminating zero */
			*--name = 0;	/* overwrite '.' with terminating 0  */
			break;
		} else if((c & 0xc0) == 0xc0) {
			char *p;	/* it's a pointer */
			c = ((c & 0x3f)<<8) + *(*uppb)++;	/* offset from buf */
			p = buf+c;
			return dns_decompress(name, nameend, &p, buf, bufend); /* recursive call */
		} else {			/* must be 1-byte label length */
			if((*uppb + c) >= (unsigned char *)bufend) {
				return ILL_FORMED_INPUT;	/* error! corrupted packet */
			}
			if((name + c + 1) >= nameend) {
				return NAME_TOO_LONG;	/* error! name > 255 chars?? */
			}
			memcpy(name, *uppb, c);
			name += c;
			*name++ = '.';
			*uppb += c;
		}
	}
	return OK;
}

/* decodes RR fields. Offsets are relative to buf. rdata is NOT parsed, but kept raw. */
static dns_parse_status dns_rr_parse(dns_rr *pdnsrr, char **ppb, char *buf, char *bufend) {
	dns_parse_status status;
	unsigned char **uppb = (unsigned char **)ppb;
	status = dns_decompress(pdnsrr->name, pdnsrr->name+255, ppb, buf, bufend);
	if(status == OK) {
		pdnsrr->type = *(*uppb)++;
		pdnsrr->type = (pdnsrr->type << 8) + *(*uppb)++;
		pdnsrr->class = *(*uppb)++;
		pdnsrr->class = (pdnsrr->class << 8) + *(*uppb)++;
		pdnsrr->ttl = *(*uppb)++;
		pdnsrr->ttl = (pdnsrr->ttl << 8) + *(*uppb)++;
		pdnsrr->ttl = (pdnsrr->ttl << 8) + *(*uppb)++;
		pdnsrr->ttl = (pdnsrr->ttl << 8) + *(*uppb)++;
		pdnsrr->rdatalen =  *(*uppb)++;
		pdnsrr->rdatalen = (pdnsrr->rdatalen << 8) + *(*uppb)++;
		pdnsrr->rdata = malloc(pdnsrr->rdatalen);
		assert(pdnsrr->rdata != NULL);
		memcpy(pdnsrr->rdata, *uppb, pdnsrr->rdatalen);
		*uppb += pdnsrr->rdatalen;
	}
	return status;
}


/* decodes question fields. Offsets are relative to buf. */
static dns_parse_status dns_question_parse(dns_question *pdnsq, char **ppb, char *buf, char *bufend) {
	dns_parse_status status;
	unsigned char **uppb = (unsigned char **)ppb;
	unsigned char *raw_start = *uppb;
	status = dns_decompress(pdnsq->name, pdnsq->name+255, ppb, buf, bufend);
	if(status == OK) {
		pdnsq->type = *(*uppb)++;
		pdnsq->type = (pdnsq->type << 8) + *(*uppb)++;
		pdnsq->class = *(*uppb)++;
		pdnsq->class = (pdnsq->class << 8) + *(*uppb)++;
	}
	pdnsq->raw_length = *uppb - raw_start;
	pdnsq->raw = malloc(pdnsq->raw_length);
	assert(pdnsq->raw != NULL);
	memcpy(pdnsq->raw, raw_start, pdnsq->raw_length);
	return status;
}

int bufscan(char *name, unsigned short int labeloffs[], int nlabeloffs, int *pilabeloffs, char *buf, char *bufend) {
	char temp_decompr[512];
	char *pb;
	int i;

	for(i=0; i < *pilabeloffs; i++) {
		pb = buf+labeloffs[i];
		if(dns_decompress(temp_decompr, temp_decompr+sizeof(temp_decompr), &pb, buf, bufend) == OK) {
			if(strcasecmp(name, temp_decompr) == 0) {
				/* found match! */
				return labeloffs[i];
			}
		}
	}
	return -1;
}


/* pack a domain name into a RFC1035 representation as sequence of length-
   prefixed labels terminated by a zero byte.
   returns a struct (NOT a pointer to struct) containing:
   in the member buf a pointer to the malloc'd packed domain name (NULL if error),
   and in the member buflen the number of bytes used by buf */
packedname dns_domain_pack(char *name) {
	char *bufend;
	char *pb;
	packedname pn;
	pn.buflen = 1024;	/* play it safe */
	if(name == NULL)
		goto errnm;
	pn.buf = pb = malloc(pn.buflen);
	assert(pn.buf != NULL);
	bufend = pn.buf + pn.buflen;
	for(;;) {	/* for all labels in name string */
		char *thislabelstart = pb;
		if(pb >= bufend)
			goto err;
		*pb++ = 0;	/*	leave space for label length prefix, or terminator */
		if(*name == 0)
			break;	/* no more labels, job done */
		for(;;) {	/* for all characters in the label */
			if(pb >= bufend)
				goto err;
			if(*name == 0 || *name == '.') {
				*thislabelstart = pb - thislabelstart - 1;
				if(*name == '.')
					name++;
				break;	/* this label completed */
			} else {
				if(pb >= bufend)
					goto err;
				*pb++ = *name++;
			}
		}
	}
	pn.buflen = pb - pn.buf;
	pn.buf = realloc(pn.buf, pn.buflen);
	return pn;
err:
	free(pn.buf);
errnm:
	pn.buflen = 0;
	pn.buf = NULL;
	return pn;
}

static int dns_rr_pack(char **ppb, char *bufend, dns_rr *pdnsrr) {
	packedname pn;
	if(pdnsrr->class != I)
		return 1;
	pn = dns_domain_pack(pdnsrr->name);
	if(pn.buf == NULL)
		return 2;
	if(*ppb + 10 + pn.buflen + pdnsrr->rdatalen >= bufend)
		return 4;
	memcpy(*ppb, pn.buf, pn.buflen);
	free(pn.buf);
	*ppb += pn.buflen;
	*(*ppb)++ = (pdnsrr->type     >>  8) & 0xff;
	*(*ppb)++ = (pdnsrr->type          ) & 0xff;
	*(*ppb)++ = (pdnsrr->class    >>  8) & 0xff;
	*(*ppb)++ = (pdnsrr->class         ) & 0xff;
	*(*ppb)++ = (pdnsrr->ttl      >> 24) & 0xff;
	*(*ppb)++ = (pdnsrr->ttl      >> 16) & 0xff;
	*(*ppb)++ = (pdnsrr->ttl      >>  8) & 0xff;
	*(*ppb)++ = (pdnsrr->ttl           ) & 0xff;
	*(*ppb)++ = (pdnsrr->rdatalen >>  8) & 0xff;
	*(*ppb)++ = (pdnsrr->rdatalen      ) & 0xff;
	memcpy(*ppb, pdnsrr->rdata, pdnsrr->rdatalen);
	*ppb += pdnsrr->rdatalen;
	return 0;
}

static int dns_question_pack(char **ppb, char *bufend, dns_question *pdnsq) {
	packedname pn;
	if(pdnsq->class != I)
		return 1;
	pn = dns_domain_pack(pdnsq->name);
	if(pn.buf == NULL)
		return 2;
	if(*ppb + 4 + pn.buflen >= bufend)
		return 4;
	memcpy(*ppb, pn.buf, pn.buflen);
	free(pn.buf);
	*ppb += pn.buflen;
	*(*ppb)++ = (pdnsq->type     >>  8) & 0xff;
	*(*ppb)++ = (pdnsq->type          ) & 0xff;
	*(*ppb)++ = (pdnsq->class    >>  8) & 0xff;
	*(*ppb)++ = (pdnsq->class         ) & 0xff;
	return 0;
}
