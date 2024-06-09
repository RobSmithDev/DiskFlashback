
#include <string.h>
#include <stdio.h>
#include "exec_nodes.h"
#include "blocks.h"
#include "struct.h"


#define D(x)


bool W_MatchPatternNoCase(STRPTR pat, STRPTR str, struct globaldata *g)
{	
	return false;
}

STRPTR W_FilePart(STRPTR path, struct globaldata *g)
{
	if(path)
	{
		STRPTR i;
		if (!*path)
			return (STRPTR)path;
		i = path + strlen (path) -1;
		while ((*i != ':') && (*i != '/') && (i != path))
			i--;
		if ((*i == ':')) i++;
		if ((*i == '/')) i++;
		return (STRPTR)i;
	}
	return NULL;
}

STRPTR W_PathPart(STRPTR path, struct globaldata *g)
{
	STRPTR ptr;

	while (*path == '/')
	{
		++path;
	}
	ptr = path;
	while (*ptr)
	{
		if (*ptr == '/')
		{
			path = ptr;
		}
		else if (*ptr == ':')
		{
			path = ptr + 1;
		}
		ptr++;
	}
	return (STRPTR)path;
}

LONG W_ErrorReport(LONG code, LONG type, ULONG arg1, struct MsgPort *device, struct globaldata *g)
{
	return false;
}

