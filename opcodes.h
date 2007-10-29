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

/* max number of non-responses before a node is assumed dead or offline */
#define NONRESPONSE_THRESHOLD 2	/* traditionally: 5 */

/* eMule KAD */
#define OP_KADEMLIAHEADER		0xE4
#define OP_KADEMLIAPACKEDPROT	0xE5

#define KADEMLIA_BOOTSTRAP_REQ	0x00	/* <PEER (sender) [25]>				*/
#define KADEMLIA_BOOTSTRAP_RES	0x08	/* <CNT [2]> <PEER [25]>*(CNT)		*/
#define KADEMLIA_HELLO_REQ	 	0x10	/* <PEER (sender) [25]>				*/
#define KADEMLIA_HELLO_RES     	0x18	/* <PEER (reciever) [25]>			*/
#define KADEMLIA_FIREWALLED_REQ	0x50	/* <TCPPORT (sender) [2]>			*/
#define KADEMLIA_FIREWALLED_RES	0x58	/* <IP (sender) [4]>				*/
#define KADEMLIA_FIREWALLED_ACK	0x59	/* (null) (sent if TCP connection to the declared port did actually succeed)	*/
#define KADEMLIA_REQ		   	0x20	/* <TYPE [1]> <HASH (target) [16]> <HASH (reciever) 16> */
#define KADEMLIA_RES			0x28	/* <HASH (target) [16]> <CNT> <PEER [25]>*(CNT) */
#define KADEMLIA_SEARCH_REQ		0x30	/* <HASH (key) [16]> <ext 0/1 [1]> <SEARCH_TREE>[ext] */
#define KADEMLIA_SEARCH_RES		0x38	/* <HASH (key) [16]> <CNT1 [2]> (<HASH (answer) [16]> <CNT2 [2]> <META>*(CNT2))*(CNT1) */
#define KADEMLIA_PUBLISH_REQ	0x40	/* <HASH (key) [16]> <CNT1 [2]> (<HASH (target) [16]> <CNT2 [2]> <META>*(CNT2))*(CNT1) */
#define KADEMLIA_PUBLISH_RES	0x48	/* <HASH (key) [16]>				*/
#define KADEMLIA_BUDDY_REQ		0x51	/* <TCPPORT (sender) [2]>			*/
#define KADEMLIA_BUDDY_CON		0x52	/* 									*/
#define KADEMLIA_BUDDY_ACK		0x57	/* <TCPPORT (sender) [2]>			*/

/* KADEMLIA (parameter for KADEMLIA_REQ)
   note: the three most sign. bits are reserved for future flags */
#define KADEMLIA_FIND_VALUE		0x02
#define KADEMLIA_STORE			0x04
#define KADEMLIA_FIND_NODE		0x0B

/***************************************************************************/

/* RevConnect */
#define OP_REVCONNHEADER		0xD0
#define OP_REVCONNPACKEDPROT	0xD1

/***************************************************************************/

/* Overnet  - using symbolic names derived from Ethereal's plugin */
/* In Overnet PEER is 23 byte long, as TCPport is missing */
#define OP_EDONKEYHEADER		0xE3

#define OVERNET_CONNECT                     0x0A  /* > <PEER (sender)[23]> [2+23=25]				*/
#define OVERNET_CONNECT_REPLY               0x0B  /* < <LEN[2]> <PEER [23]>[len]	[2+2+23*LEN]		*/

#define OVERNET_PUBLICIZE                   0x0C  /* > <PEER (sender)[23]> [2+23=25]				*/ /* OvernetPublicize in mldonkey */
#define OVERNET_PUBLICIZE_ACK               0x0D  /* < (null) [2+0=2]								*/

#define OVERNET_SEARCH                      0x0E  /* > <PARAMETER[1]> <HASH> [2+1+16=19]			*/ /* OvernetSearch in mldonkey */
#define OVERNET_SEARCH_NEXT                 0x0F  /* < <HASH> <LEN[1]> <PEER>[len] 2+16+1+23*LEN]	*/ /* OvernetSearchReply in mldonkey; LEN <= 4 */

/* if the peer lists itself in the OVERNET_SEARCH_NEXT reply, the initiator may send it a OVERNET_SEARCH_INFO */
#define OVERNET_SEARCH_INFO                 0x10  /* > <HASH> 0 <MIN> <MAX> [2+16+1+2+2 = 23]		*/ /* OvernetGetSearchResults in mldonkey */
												  /*   <HASH> 1 <SEARCH_TREE> <MIN> <MAX> */ /* OvernetGetSearchResults in mldonkey */
#define OVERNET_SEARCH_RESULT               0x11  /* < <SEARCH_HASH> <FILE_HASH> <CNT4> <META>[cnt]*//* OvernetSearchResult in mldonkey. Zero or more packets, followed by a OVERNET_SEARCH_END  */
/* the two ushorts are listed by OvernetClc:
> s fat day -Audio
> Got results. (2650,2650)
  Got results. (4564,4564)
Meaning?? */
#define OVERNET_SEARCH_END                  0x12  /* < <HASH> <two ushorts> [2+16+4=22]*/	/* OvernetNoResult in mldonkey */

/* cDonkey only publishes 2 METAs: filename (EDONKEY_STAG_NAME) and filesize (EDONKEY_STAG_SIZE) */
/* OvernetClc publishes filename, filesize, filetype (EDONKEY_STAG_TYPE) fileformat (EDONKEY_STAG_FORMAT) and availability (EDONKEY_STAG_AVAILABILITY) */
#define OVERNET_PUBLISH                     0x13  /* >  <kwHASH> <fileHASH> <CNT4> <META>[cnt] (publishing filename, filetype, fileformat, filesize...)
														<fileHASH> <myHASH> <CNT4> <META>[cnt] (publishing loc) */	/* OvernetPublish in mldonkey */
#define OVERNET_PUBLISH_ACK                 0x14  /* < <firstHASHinPublish>	[2+16=18]						*/	/* OvernetPublished in mldonkey */

/* A good use of OVERNET_IDENTIFY is to learn hash and subjective IP/UDP port of a peer which has
   become known through an unsolicited request not carrying sender peernode data,
   typically OVERNET_SEARCH, OVERNET_SEARCH_INFO, OVERNET_PUBLISH, OVERNET_IP_QUERY
   This may be the single point where the NATted status of a peer is checked.
   If a peer is found to be NATted, it should NOT be added to kspace/kbuckets
   or to contacts rbt, because generally it will be impossible to query. */
#define OVERNET_IDENTIFY                    0x1E  /* > (null)	[2+0=2]							*/ /* (not defined in mldonkey) */
#define OVERNET_IDENTIFY_REPLY              0x15  /* < <CONTACT (sender)> [2+22=24]				*/	/* (not defined in mldonkey) */
#define OVERNET_IDENTIFY_ACK                0x16  /* > <PORT_tcp (sender)> [2+2=4]				*/	/* (not defined in mldonkey) */

/* please open a TCP connection to my PORT_tcp (and send me the file targetHASH?)  */
#define OVERNET_FIREWALL_CONNECTION         0x18  /* > <targetHASH> <senderTcpPORT> [2+16+2=20]	*/ /* OvernetFirewallConnection in mldonkey */
#define OVERNET_FIREWALL_CONNECTION_ACK     0x19  /* < <targetHASH>	[2+16=18]					*/ /* OvernetFirewallConnectionACK in mldonkey */
#define OVERNET_FIREWALL_CONNECTION_NACK    0x1A  /* < <targetHASH>	[2+16=18]					*/ /* OvernetFirewallConnectionNACK in mldonkey */

#define OVERNET_IP_QUERY                    0x1B  /* > <PORT_tcp> [2+2=4]						*/ /* OvernetGetMyIP in mldonkey */
#define OVERNET_IP_QUERY_ANSWER             0x1C  /* < <IP>		[2+4=6]							*/ /* OvernetGetMyIPResult in mldonkey */
#define OVERNET_IP_QUERY_END                0x1D  /* < (null)	[2+0=2] only if Tport accessible*/ /* OvernetGetMyIPDone in mldonkey */

#define OVERNET_PEER_NOTFOUND				0x21  /* > <HASH> <IP> <PORT> <FLAG> mldonkey removes the peer when it receives it*/  /* OvernetPeerNotFound in mldonkey */

/*
   PARAMETER used in OVERNET_SEARCH:
   Search for loc or metadata: uses only 0x02
   Publishing:                 uses only 0x04
   Lookup for its own hash:    uses only 0x14
   Apparently during startup
 */

#define OVERNET_FIND_SEARCH		0x02 /* use when searching for metadata or loc - NOT used during publishing. */
#define OVERNET_FIND_ONLY		0x04 /*    used during publishing */
#define OVERNET_FIND_UNKN		0x0A /*    10 ?? never seen by EM */
#define OVERNET_FIND_SELF		0x14 /*    20 - used only for node lookups of itself */


/*
	META tags are used in OVERNET_PUBLISH and OVERNET_SEARCH_RESULT.
	A META is a <tag, value> pair.
	Both tag and value are prefixed by an ushort specifying
	their length in bytes. Tags are either ASCII strings or special 1-byte
	tags with predefined meaning (see below symbols starting with
	"EDONKEY_STAG_")

     <Meta Tag> ::= <TAG_TYPE [1]> <Tag Name> <Tag>
     <Tag Name> ::= <Tag Name Size [2]> (<Special Tag> || <String>)
     <String>   ::= <LENGTH [2]> <ascii data>

     Example of tag use in publish request of a file, associated to
     two Meta tags: filename and filesize:

     OP_EDONKEYHEADER[1]
     OVERNET_PUBLISH[1]
     <data HASH[16]>			// file hash
     <my HASH[16]>				// this peer's hash
       <CNT4[4]> = 2 // Meta tag list size: two tags will follow: filename (string) and filesize (int)

         EDONKEY_MTAG_STRING[1] = 0x02	// the value of the first tag is a string
          <CNT2[2]> = 1 			// the name has length 1
          EDONKEY_STAG_NAME[1]	= 0x01 // special: says that the value is a filename
	      <CNT2[2]> = N 			// length of filename
	      <filename[N]>			// actual filename

     	 EDONKEY_MTAG_DWORD[1]	= 0x03 // the value of second tag is an unsigned long
       	  <CNT2[2]> = 1			// the name has length 1
     	  EDONKEY_STAG_SIZE[1]	// special says that the value is a filesize
     	  <CNT4[4]>				// filesize

	Other publishing may use tags like type (e.g. "image"), format (e.g. "jpg")
	availability (an unsigned long) etc.
    ==============================

	Example of tag usage in Search Tree preceding min/max in OVERNET_SEARCH_INFO:
    the tree is &(&("day","-audio"), "audio",
    00 00: OP AND { // two searches follow
    00 00: OP AND  {   // the first is an AND of two searches which follow
                      // so totally we expect TRHEE searches
	01: 	EDONKEY_SEARCH_NAME[1]
	03  00: three-byte string follows
	"day"

	01: EDONKEY_SEARCH_NAME[1]
	06 00: six-byte string follows
	"-audio"

	02: EDONKEY_SEARCH_META[1] // the last search is a META
	05 00: five-byte string follows as meta tag value
	"audio"
	01 00: one-byte (special) string follows as meta tag name
	03:   EDONKEY_STAG_TYPE[1] // type

	00 00: min = 0
	64 00: max = 100
    ==============================

	Another example with size limits:
	&(&(&("day", format=audio), min=1234), max=5678)

	00 00 (&
	00 00  (&
	00 00   (&

	01: 	EDONKEY_SEARCH_NAME[1]
	03 00: three-byte string follows
	64 61 79: "day"
	    ,
    02: EDONKEY_SEARCH_META[1] // the next search is a META
    05 00: five-byte string follows as meta tag value
    "audio"
    01 00: one-byte (special) string follows as meta tag name
    04:   EDONKEY_STAG_FORMAT[1]
            ),
    03: EDONKEY_SEARCH_LIMIT[1]
    d2 04 00 00: hex value of 32-bit 1234
    01: min
    01 00: one-byte (special) string follows as meta tag name
    02: EDONKEY_STAG_SIZE
           ),
    03: EDONKEY_SEARCH_LIMIT[1]
    2e 16 00 00 hex value of 32-bit 5678
    02: max
    01 00: one-byte (special) string follows as meta tag name
    02: EDONKEY_STAG_SIZE
          )
 */

/* EDONKEY TAG_TYPEs */
#define EDONKEY_MTAG_UNKNOWN             0x00
#define EDONKEY_MTAG_HASH                0x01
#define EDONKEY_MTAG_STRING              0x02
#define EDONKEY_MTAG_DWORD               0x03
#define EDONKEY_MTAG_FLOAT               0x04
#define EDONKEY_MTAG_BOOL                0x05
#define EDONKEY_MTAG_BOOL_ARRAY          0x06
#define EDONKEY_MTAG_BLOB                0x07

/* EDONKEY SPECIAL TAGS */
#define EDONKEY_STAG_UNKNOWN             0x00
	/* only following 4 used in Overnet: */
	/* Note: Overnet's GUI labels "format" the widget to select type;
	   OvernetClc generates "format" tags for -<Type> command options */
#define EDONKEY_STAG_NAME                0x01
#define EDONKEY_STAG_SIZE                0x02
#define EDONKEY_STAG_TYPE                0x03 /* audio, video, doc... */
#define EDONKEY_STAG_FORMAT              0x04 /* MP3, zip... */

#define EDONKEY_STAG_COLLECTION          0x05
#define EDONKEY_STAG_PART_PATH           0x06
#define EDONKEY_STAG_PART_HASH           0x07
#define EDONKEY_STAG_COPIED              0x08
#define EDONKEY_STAG_GAP_START           0x09
#define EDONKEY_STAG_GAP_END             0x0a
#define EDONKEY_STAG_DESCRIPTION         0x0b
#define EDONKEY_STAG_PING                0x0c
#define EDONKEY_STAG_FAIL                0x0d
#define EDONKEY_STAG_PREFERENCE          0x0e
#define EDONKEY_STAG_PORT                0x0f
#define EDONKEY_STAG_IP                  0x10
#define EDONKEY_STAG_VERSION             0x11
#define EDONKEY_STAG_TEMPFILE            0x12
#define EDONKEY_STAG_PRIORITY            0x13
#define EDONKEY_STAG_STATUS              0x14
#define EDONKEY_STAG_AVAILABILITY        0x15
#define EDONKEY_STAG_QTIME               0x16
#define EDONKEY_STAG_PARTS               0x17

/* SEARCH_TREE's are Forward Polish Notation expressions :

SEARCH   ::= 	0x00 <OPERATOR> <SEARCH> <SEARCH>
	   			0x01 <STRING>
	   			0x02 <META_VALUE[]> <META_NAME[]>
	   			0x03 <DWORD[4]> <MINMAX[1]> <META_NAME[]>
OPERATOR ::=	0x00 // AND
				0x01 // OR
				0x02 // NOT AND
MINMAX   ::=	0x01 // MIN
				0x02 // MAX
*/

/* EDONKEY SEARCH TYPES */

/* <Search> ::=  <Operator> <Search Query> <Search Query> */
#define EDONKEY_SEARCH_BOOL              0x00

/* <Search> ::=  <String> */
#define EDONKEY_SEARCH_NAME              0x01

/* <Search> ::=  <String> <Meta tag Name> */
#define EDONKEY_SEARCH_META              0x02

/* <Search> ::=  <SizeLimit (guint32)> <Minmax (1)> <Meta tag Name> */
#define EDONKEY_SEARCH_LIMIT             0x03

/* EDONKEY SEARCH OPERATORS */
#define EDONKEY_SEARCH_AND               0x00
#define EDONKEY_SEARCH_OR                0x01
#define EDONKEY_SEARCH_ANDNOT            0x02

/* EDONKEY SEARCH MIN/MAX   */
#define EDONKEY_SEARCH_MIN               0x01
#define EDONKEY_SEARCH_MAX               0x02


/* ----------- old cDonkey stuff ------------ */
#if 0

#define OVERNET_CONNECT			0x0A	/* <PEER (sender)[23]> [2+23=25]				*/
#define OVERNET_CONNECT_RES		0x0B	/* <LEN2> <PEER [23]>[len]	[2+2+23*LEN2]		*/
#define OVERNET_HELLO_REQUEST 	0x0C	/* <PEER (sender)[23]> [2+23=25]				*/ /* OvernetPublicize in mldonkey */
#define OVERNET_HELLO_ACK     	0x0D	/* (null) [2+0=2]								*/
#define OVERNET_REQUEST 	   	0x0E	/* <PARAMETER[1]> <HASH> [2+1+16=19]			*/ /* OvernetSearch in mldonkey */
#define OVERNET_REQUEST_RES		0x0F	/* <HASH> <LEN2> <PEER>[len] 2+16+2+23*LEN2]	*/ /* OvernetSearchReply in mldonkey */
#define OVERNET_SEARCH_REQUEST  0x10	/* <HASH> 0 <MIN> <MAX> [2+16+1+2+2 = 23]		*/ /* OvernetGetSearchResults in mldonkey */
										/* <HASH> 1 <SEARCH_TREE> <MIN> <MAX>			*/ /* OvernetGetSearchResults in mldonkey */
#define OVERNET_SEARCH_RESULT	0x11	/* <SEARCH_HASH> <RETURN_HASH> <CNT4> <META>[cnt]	*/
#define OVERNET_SEARCH_END		0x12	/* <HASH> <IP?>	[2+16+4=22]					*/	/* OvernetNoResult in mldonkey */
#define OVERNET_PUBLISH_REQ		0x13	/* <HASH> <HASH> <CNT4> <META>[cnt]			*/
#define OVERNET_PUBLISH_ACK		0x14	/* <HASH>	[2+16=18]						*/
#define OVERNET_IDENTIFY_RES	0x15	/* <CONTACT (sender)> [2+22=24]				*/
#define OVERNET_IDENTIFY_ACK	0x16	/* <PORT_tcp (sender)> [2+2=4]				*/
#define OVERNET_FIREWALL_REQ	0x18	/* <HASH> <PORT_tcp> [2+16+2=20]			*/ /* OvernetFirewallConnection in mldonkey */
#define OVERNET_FIREWALL_ACK	0x19	/* <HASH>	[2+16=18]						*/ /* OvernetFirewallConnectionACK in mldonkey */
#define OVERNET_FIREWALL_NACK	0x1A	/* <HASH>	[2+16=18]						*/ /* OvernetFirewallConnectionNACK in mldonkey */
#define OVERNET_IP_REQ			0x1B	/* <PORT_tcp> [2+2=4]						*/ /* OvernetGetMyIP in mldonkey */
#define OVERNET_IP_RES			0x1C	/* <IP>		[2+4=6]							*/ /* OvernetGetMyIPResult in mldonkey */
#define OVERNET_IP_ACK			0x1D	/* (null)	[2+0=2]							*/ /* OvernetGetMyIPDone in mldonkey */
#define OVERNET_IDENTIFY_REQ	0x1E	/* (null)	[2+0=2]							*/ /* (not defined in mldonkey) */


/*
Connect		0x0A <PEER (sender)>				(25 Byte)
Connect_reply	0x0B <LEN2> <PEER>[len]
Publicize	0x0C <PEER (sender)>				(2 +16+4+2+1=25 Byte)
Publicize_ack	0x0D (null)					( 2 Byte)
Search		0x0E <PARAM> <HASH>				(2 +1+16=19 Byte)	// UNTRUE!!!->CNT tell the number of whished answers
Search_next	0x0F <HASH> <LEN2> <PEER>[len]			// min len should be 2
Search_info  	0x10 <HASH> 0 <MIN> <MAX>			(23 Bytes)
	or:		  	0x10 <HASH> 1 <SEARCH> <MIN> <MAX>	(var. Bytes)
Search_result	0x11 <SEARCH_HASH> <RETURN_HASH> <CNT4> <META>[cnt]		// if search was keyword they can divergate
Search_end	0x12 <HASH> <IP?>
Publish		0x13 <HASH> <HASH> <CNT4> <META>[cnt]
Publish_ack	0x14 <HASH>
Identify_reply	0x15 <CONTACT (sender)>				// Answer to 0x1E
Identify_ack	0x16 <PORT_tcp (sender)>			// Answer to 0x15
FireCon		0x18 <HASH> <PORT_tcp>				// hash of the firewall server
FireCon_ack	0x19 <HASH>
FireCon_nack	0x1A <HASH>
IP_query	0x1B <PORT_tcp>
IP_answer	0x1C <IP>
IP_end		0x1D (null)
Identify	0x1E (null)				// request the other side to tell hash/ip/port

CONTACT ::= <HASH><IP><PORT (udp)>  (22 Byte)
PEER	::= <CONTACT><KIND> (23 Byte)
HASH 	::= 16 Byte value (for files and tree-md4)
IP	::=  4 Byte value (IP in network order)
PORT	::=  2 Byte value port
KIND	::=  1 Byte (what is the exact meaning??)
LEN2	::=  2 Byte "unsigned short"
CNT4	::=  4 Byte "unsigned int"
META	::= Meta Tag <mTYPE> <mLEN> <mKEY> <mValue> see eDonkey Protocol
mTYPE	::=  1 Byte
mLEN	::=  2 Byte
mKEY	::=  mLen Bytes
mValue	::=  * Bytes (depend on mTYPE)

Substring search:
- stransform to lower text
- use " ()[]{}<>,._-!?" as Wort seperator
- ignore file extensions
- ignore 1 and 2 Letter words
- use the md4(keyword) as hash (Thomas-Lussnig => TIP: use the largest word (most significant))

SEARCH:
- basic search (0x0F) algorithm is used for source,substring,publish find and publish
- depend on the type once info(0x10) where send in the other case publicize(0x0C)
or publish(0x13) so the nearest nodes become their information.
- then you send (0x0F) to nearer nodes till you got 20 best or timeout
- if a node cannot suply better ones take them as temporary best and send detail query/publish
- until you have 20 best the better replace the nodes where log(2)(distance) is worst.

Next Client to Ask:
- where the log(2) (XOR(seek_hash,client_hash)) is smaller and not already asked

Tag:
"loc"		=> "bcp://217.233.215.74:4662"
"filename"	=> string
"size"		=> int
"ip"		=> int
"port"		=> int

*/

/* ----------- end old cDonkey stuff ------------ */
#endif
