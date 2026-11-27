#include "cg_local.h"
#include "../qcommon/qcommon.h"

typedef struct chatFilterTableNode_s
{
	char* name;
	struct chatFilterTableNode_s* next;
	struct chatFilterTableNode_s* prev;
} chatFilterTableNode_t;

#define CG_CHATFILTER_MAX 128

static chatFilterTableNode_t chatFilterNames[CG_CHATFILTER_MAX];

static int ChatReadableCopy(const char* in, char* out, int outSize);

/*
Names are filed and compared by their readable text (colour codes and chat
escape characters skipped): a player is muted under the coded name of the
player string, while xq3osp chat lines carry the name without colour codes
*/
static chatFilterTableNode_t* CG_ChatfilterFindName(const char* name)
{
	unsigned long hash;
	chatFilterTableNode_t* target;
	char readable[MAX_CLIENTNAME];

	if (!ChatReadableCopy(name, readable, sizeof(readable)))
	{
		return NULL; //nothing readable in the name
	}

	hash = Com_GenerateHashValue(readable, CG_CHATFILTER_MAX);

	target = &chatFilterNames[hash];

	while (target && (!target->name || Q_stricmp(readable, target->name)))
	{
		target = target->next;
	}

	return target; //NULL when not found
}

void CG_ChatfilterDump(void)
{
	int p;
	char nameCodesBuf[MAX_QPATH * 2];

	CG_Printf("^7| ^1%2s ^7| ^5%5s | ^5%32s | ^5%s\n", "ID", "Muted", "Player name", "Player name raw");
	CG_Printf("^7------------------------------------------------------------------------------\n");
	for (p = 0; p < MAX_CLIENTS; ++p)
	{
		if (cgs.clientinfo[p].infoValid)
		{
			CG_StringMakeEscapeCharRAW(cgs.clientinfo[p].name_original, nameCodesBuf, sizeof(nameCodesBuf));
			CG_Printf("^7| ^1%2d ^7|  %s  ^7| %32s | %s\n", p,
			          CG_ChatfilterIsNameMuted(cgs.clientinfo[p].name_original) ? "^1Yes" : "^2No ",
			          cgs.clientinfo[p].name_clean, nameCodesBuf);
		}
	}
}

qboolean CG_ChatfilterIsNameMuted(const char* name)
{
	return CG_ChatfilterFindName(name) != NULL;
}

static void CG_ChatfilterChatWindowNotice(int clientNum, qboolean muted)
{
	char          line[BE_CHAT_LINE_CHARS];
	clientInfo_t* ci;

	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return;
	}

	ci = &cgs.clientinfo[clientNum];
	if (!ci->infoValid)
	{
		return;
	}

	line[0] = '\0';
	Q_strncpyz(line, ci->name, sizeof(line));
	if (muted)
	{
		Q_strcat(line, sizeof(line), " - ^1Muted");
	}
	else
	{
		Q_strcat(line, sizeof(line), " - ^2Unmuted");
	}
	CG_BEChat_AddLine(line, clientNum, BE_CHAT_MSG_OTHER);
}

void CG_ChatfilterMuteClient(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS || !cgs.clientinfo[clientNum].infoValid)
	{
		return;
	}

	CG_ChatfilterAddName(cgs.clientinfo[clientNum].name_original);
	CG_ChatfilterSaveFile(CG_CHATFILTER_DEFAULT_FILE);
	CG_ChatfilterChatWindowNotice(clientNum, qtrue);
}

void CG_ChatfilterUnmuteClient(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS || !cgs.clientinfo[clientNum].infoValid)
	{
		return;
	}

	CG_ChatfilterDeleteName(cgs.clientinfo[clientNum].name_original);
	CG_ChatfilterSaveFile(CG_CHATFILTER_DEFAULT_FILE);
	CG_ChatfilterChatWindowNotice(clientNum, qfalse);
}

void CG_ChatfilterDeleteName(const char* name)
{
	unsigned long hash;
	chatFilterTableNode_t* target;
	char readable[MAX_CLIENTNAME];

	if (!ChatReadableCopy(name, readable, sizeof(readable)))
	{
		return;
	}

	target = CG_ChatfilterFindName(name);
	if (!target)
	{
		return;
	}

	hash = Com_GenerateHashValue(readable, CG_CHATFILTER_MAX);

	if (target == &chatFilterNames[hash])
	{
		//root node
		if (target->name) Z_Free(target->name);
		target->name = NULL;
		if (target->next)
		{
			target->name = target->next->name;
			target->next = target->next->next;
		}
	}
	else
	{
		if (target->prev)
		{
			target->prev->next = target->next;
		}
		if (target->next)
		{
			target->next->prev = target->prev;
		}
		Z_Free(target->name);
		Z_Free(target);
	}
}

void CG_ChatfilterAddName(const char* name)
{
	unsigned long hash;
	chatFilterTableNode_t* target;
	char readable[MAX_CLIENTNAME];
	int len;

	len = ChatReadableCopy(name, readable, sizeof(readable));
	if (!len)
	{
		return;
	}

	if (CG_ChatfilterFindName(name))
	{
		return;
	}

	hash = Com_GenerateHashValue(readable, CG_CHATFILTER_MAX);

	target = &chatFilterNames[hash];

	while (target->name)
	{
		if (!target->next)
		{
			target->next = Z_Malloc(sizeof(chatFilterTableNode_t));
			OSP_MEMORY_CHECK(target->next);
			memset(target->next, 0, sizeof(chatFilterTableNode_t));
		}
		target->next->prev = target;
		target = target->next;
	}

	target->name = Z_Malloc(len + 1);
	OSP_MEMORY_CHECK(target->name);

	Q_strncpyz(target->name, readable, len + 1);
}

qboolean CG_ChatExtractSenderName(const char* message, char* name, int nameSize)
{
	const char* splitter;
	const char* start;
	int len;

	if (!message || !name || nameSize <= 0)
	{
		return qfalse;
	}

	start = message;
	if (start[0] == 25 && (start[1] == '(' || start[1] == '['))
	{
		start += 2;
	}

	splitter = strchr(start, 25);
	if (!splitter)
	{
		return qfalse;
	}

	splitter -= 2;
	len = splitter - start;
	if (len <= 0 || len >= nameSize)
	{
		return qfalse;
	}

	memcpy(name, start, len);
	name[len] = 0;
	return qtrue;
}

static int CG_ChatClientNumFromBracketId(const char* senderName)
{
	const char* rb;
	int clientNum;

	if (!senderName || senderName[0] != '[')
	{
		return -1;
	}

	rb = strchr(senderName, ']');
	if (!rb)
	{
		return -1;
	}

	clientNum = atoi(senderName + 1);
	if (clientNum < 0 || clientNum >= MAX_CLIENTS || !cgs.clientinfo[clientNum].infoValid)
	{
		return -1;
	}

	return clientNum;
}

static qboolean ChatIsHexColor(int c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int ChatLower(int c)
{
	if (c >= 'A' && c <= 'Z')
	{
		return c + 32;
	}
	return c;
}

/* past the colour codes and chat escape characters at s */
static const char* ChatSkipColors(const char* s)
{
	while (s[0] == 25 || (s[0] == '^' && s[1]))
	{
		if (s[0] == 25)
		{
			s++;
			continue;
		}
		if ((s[1] == 'x' || s[1] == 'X') &&
		        ChatIsHexColor(s[2]) && ChatIsHexColor(s[3]) && ChatIsHexColor(s[4]) &&
		        ChatIsHexColor(s[5]) && ChatIsHexColor(s[6]) && ChatIsHexColor(s[7]))
		{
			s += 6;
		}
		else if (s[1] == '^')
		{
			break;      /* escaped caret: a readable character */
		}
		s += 2;
	}
	return s;
}

/* a readable caret at s: ^^ (escaped) or a lone ^ at the end; its byte length, 0 when s is not one */
static int ChatCaretLen(const char* s)
{
	if (s[0] != '^')
	{
		return 0;
	}
	return s[1] == '^' ? 2 : 1;
}

/*
Readable text of 'part' against the one of 'full', colour codes ignored on
both sides. With 'shortened' the match also succeeds when at least 32 readable
bytes of 'full' were matched and 'part' ended first: xq3osp servers cut a long
be_name down to the short name's budget in the chat lines
*/
static qboolean ChatNameMatches(const char* part, const char* full, qboolean shortened)
{
	int matched = 0;

	while (qtrue)
	{
		int caretPart, caretFull;

		part = ChatSkipColors(part);
		full = ChatSkipColors(full);
		if (!part[0] || !full[0])
		{
			break;
		}
		caretPart = ChatCaretLen(part);
		caretFull = ChatCaretLen(full);
		if (caretPart || caretFull)
		{
			/* a caret is one readable character however it is spelled */
			if (!caretPart || !caretFull)
			{
				return qfalse;
			}
			part += caretPart;
			full += caretFull;
			matched++;
			continue;
		}
		if (ChatLower((unsigned char)part[0]) != ChatLower((unsigned char)full[0]))
		{
			return qfalse;
		}
		part++;
		full++;
		matched++;
	}

	if (part[0] == 0 && full[0] == 0)
	{
		return qtrue;
	}
	return shortened && part[0] == 0 && matched >= 32;
}

/* readable text of a name into out; returns its length */
static int ChatReadableCopy(const char* in, char* out, int outSize)
{
	int o = 0;

	while (in[0] && o < outSize - 1)
	{
		int len;

		in = ChatSkipColors(in);
		if (!in[0])
		{
			break;
		}
		len = ChatCaretLen(in);
		if (len)
		{
			out[o++] = '^';
			in += len;
			continue;
		}
		out[o++] = *in++;
	}
	out[o] = 0;

	return o;
}

int CG_ChatClientNumFromSenderName(const char* senderName)
{
	int i;
	clientInfo_t* ci;

	if (!senderName || !senderName[0])
	{
		return -1;
	}

	i = CG_ChatClientNumFromBracketId(senderName);
	if (i >= 0)
	{
		return i;
	}

	/* exact: coded (older servers name the sender in the player string's form) or
	   by readable text (xq3osp chat lines carry the name without colour codes) */
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		ci = &cgs.clientinfo[i];
		if (!ci->infoValid)
		{
			continue;
		}

		if (Q_stricmp(ci->name, senderName) == 0 || Q_stricmp(ci->name_original, senderName) == 0 ||
		        ChatNameMatches(senderName, ci->name, qfalse) ||
		        ChatNameMatches(senderName, ci->name_original, qfalse))
		{
			return i;
		}
	}

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		ci = &cgs.clientinfo[i];
		if (!ci->infoValid)
		{
			continue;
		}

		if (ChatNameMatches(senderName, ci->name, qtrue) ||
		        ChatNameMatches(senderName, ci->name_original, qtrue))
		{
			return i;
		}
	}

	return -1;
}

int CG_ChatResolveSenderClientNum(const char* rawMessage, int serverClientNum)
{
	char senderName[MAX_CLIENTNAME];

	if (serverClientNum >= 0 && serverClientNum < MAX_CLIENTS && cgs.clientinfo[serverClientNum].infoValid)
	{
		return serverClientNum;
	}

	if (!CG_ChatExtractSenderName(rawMessage, senderName, sizeof(senderName)))
	{
		return -1;
	}

	CG_RemoveChatEscapeChar(senderName);
	return CG_ChatClientNumFromSenderName(senderName);
}

messageAllowed_t CG_ChatCheckMessageAllowed(const char* message)
{
	const chatFilterTableNode_t* target;
	char name[MAX_CLIENTNAME];

	if (!CG_ChatExtractSenderName(message, name, sizeof(name)))
	{
		return MESSAGE_ALLOWED_OTHER;
	}

	target = CG_ChatfilterFindName(name);

	if (!target)
	{
		/* a long be_name is cut in the chat lines: look the sender up as a
		   client and test the names the player string carries */
		int clientNum = CG_ChatClientNumFromSenderName(name);

		if (clientNum >= 0)
		{
			target = CG_ChatfilterFindName(cgs.clientinfo[clientNum].name_original);
		}
	}

	return target == NULL ? MESSAGE_ALLOWED_PLAYER : MESSAGE_NOTALLOWED;
}

void CG_ChatfilterLoadFile(const char* filename)
{
	int rc;
	char path[MAX_QPATH];
	fileHandle_t filterFileHandle;
	char* fileContent;
	const char* ptr;
	char name[MAX_QPATH];
	int namesCounter = 0;

	Com_sprintf(path, MAX_QPATH, "%s.txt", filename);

	rc = trap_FS_FOpenFile(path, &filterFileHandle, FS_READ);
	if (rc < 0 || !filterFileHandle)
	{
		return;
	}

	if (rc == 0)
	{
		trap_FS_FCloseFile(filterFileHandle);
		return;
	}

	fileContent = Z_Malloc(rc + 1);
	OSP_MEMORY_CHECK(fileContent);

	trap_FS_Read(fileContent, rc, filterFileHandle);
	trap_FS_FCloseFile(filterFileHandle);
	fileContent[rc] = 0;

	ptr = fileContent;
	do
	{
		ptr = CG_LoadLine(ptr, &name[0], sizeof(name));
		if (name[0])
		{
			//add name
			CG_ChatfilterAddName(name);
			++namesCounter;
		}
	}
	while (*ptr);

	if (namesCounter > 0)
	{
		CG_Printf("^3>>> (%s):^5 %i name%s loaded.\n", path, namesCounter, namesCounter > 1 ? "s" : "");
	}

	Z_Free(fileContent);
}


void CG_ChatfilterSaveFile(const char* filename)
{
	int rc;
	char path[MAX_QPATH];
	fileHandle_t filterFileHandle;
	int i;
	chatFilterTableNode_t* ptr;

	Com_sprintf(path, MAX_QPATH, "%s.txt", filename);

	rc = trap_FS_FOpenFile(path, &filterFileHandle, FS_WRITE);
	if (rc < 0 || !filterFileHandle)
	{
		CG_Printf("^1chat filter file %s not found\n", path);
		return;
	}


	for (i = 0; i < CG_CHATFILTER_MAX; ++i)
	{
		ptr = &chatFilterNames[i];

		while (ptr->name)
		{
			trap_FS_Write(ptr->name, strlen(ptr->name), filterFileHandle);
			trap_FS_Write("\n", 1, filterFileHandle);
			if (ptr->next == NULL) break;
			ptr = ptr->next;
		}
	}

	trap_FS_FCloseFile(filterFileHandle);
}
