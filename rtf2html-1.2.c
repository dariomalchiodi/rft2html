
/******************************************************************************
       Copyright (C) 2010 Dario Malchiodi <malchiodi@di.unimi.it>

This file is part of rtf2html.
rtf2html is free software; you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 2.1 of the License, or (at your option)
any later version.
rtf2html is distributed in the hope that it will be useful, but without any
warranty; without even the implied warranty of merchantability or fitness
for a particular purpose. See the GNU Lesser General Public License for
more details.
You should have received a copy of the GNU Lesser General Public License
along with rtf2html; if not, see <http://www.gnu.org/licenses/>.

******************************************************************************/

/*RTF2HTML.c, Chuck Shotton - 6/21/93 */
/************************************************************************
 * This program takes a stab at converting RTF (Rich Text Format) files
 * into HTML. There are some limitations that keep RTF from being able to
 * easily represent things like in-line images and anchors as styles. In
 * particular, RTF styles apply to entire "paragraphs", so anchors or
 * images in the middle of a text stream can't easily be represented by
 * styles. The intent is to ultimately use something like embedded text
 * color changes to represent these constructs. 
 * 
 * In the meantime, you can take existing Word documents, apply the
 * correct style sheet, and convert them to HTML with this tool.
 *
 * AUTHOR: Chuck Shotton, UT-Houston Academic Computing,
 *         cshotton@oac.hsc.uth.tmc.edu
 *         
 *         Dmitry Potapov, CapitalSoft
 *         dpotapov@capitalsoft.com
 *
 * USAGE: rtf2html [rtf_filename] 
 *
 * BEHAVIOR:
 *        rtf2html will open the specified RTF input file or read from
 *        standard input, writing converted HTML to standard output.
 *
 * NOTES:
 *        The RTF document must be formatted with a style sheet that has
 *        style numberings that conform to the style_mappings table
 *        defined in this source file.
 *
 * MODIFICATIONS:
 *         6/21/93 : Chuck Shotton - created version 1.0.
 *        11/26/98 : Dmitry Potapov - version 1.1 beta
 *         8/08/02 : Dario Malchiodi - version 1.2
 *
 ************************************************************************/

/* Note, the source is formated with 4 character tabs */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef _MSC_VER
#	define	strcasecmp _stricmp
#endif

#ifndef TRUE
#define TRUE -1
#define FALSE 0
#endif

#define MAX_LEVELS 40	/*defines the # of nested in-line styles (pairs of {})*/
#define MAX_RTF_TOKEN 40

#define MAX_INLINE_STYLES 5 /*defines # of in-line styles, bold, italic, etc.*/

typedef struct tag_StyleState
{
	unsigned char s: MAX_INLINE_STYLES;
} TStyleState;

typedef enum { s_plain, s_bold, s_italic, s_underline, s_hidden, /*in-line styles*/
	s_para,	s_br,	  /*pseudo style*/
	s_h0, s_h1, s_h2, s_h3, s_h4, s_h5, s_h6 /*heading styles*/
} StyleState;

char *styles[][2] = {		/*HTML Start and end tags for styles*/
	{"", ""},
	{"<b>", "</b>"},
	{"<i>", "</i>"},
	{"<u>", "</u>"},
	{"<!-- ", " -->"},
	{"<p>\n", ""},
	{"<br>\n",""},
	{"", ""},
	{"<h1>", "</h1>"},
	{"<h2>", "</h2>"},
	{"<h3>", "</h3>"},
	{"<h4>", "</h4>"},
	{"<h5>", "</h5>"},
	{"<h6>", "</h6>"}
};

/* style_mappings maps the style numbers in a RTF style sheet into one of the*/
/* (currently) six paragraph-oriented HTML styles (i.e. heading 1 through 6.)*/
/* Additional styles for lists, etc. should be added here. Style info        */
/* ultimately should be read from some sort of config file into these tables.*/

#define MAX_NAME_LEN 40
char style_name[MAX_NAME_LEN];

#define STYLE_NUMBER 7
char *style_namings[STYLE_NUMBER] = {
	"", "heading 1", "heading 2", "heading 3", "heading 4", "heading 5",
	"heading 6"
};
char style_mappings[STYLE_NUMBER][MAX_RTF_TOKEN];
char style_number[MAX_RTF_TOKEN];

/* RTF tokens that mean something to the parser. All others are ignored. */

typedef enum {
	t_start,
	t_fonttbl, t_colortbl, t_stylesheet, t_info, t_s, t_b, t_ul, t_ulw, 
	t_uld, t_uldb, t_i, t_v, t_plain, t_par, t_pict, t_tab, t_bullet, 
	t_cell, t_row, t_line, t_endash, t_emdash, 
	t_end
} TokenIndex;

char *tokens[] = {
	"###", 
	"fonttbl", "colortbl", "stylesheet", "info", "s", "b", "ul", "ulw", 
	"uld", "uldb", "i", "v", "plain", "par", "pict", "tab", "bullet",
	"cell", "row", "line", "endash", "emdash",
	"###"
};

TStyleState style_state[MAX_LEVELS], curr_style;
short curr_heading;

void (*RTF_DoControl)(char*,char*);
char isBody;
char* title;
FILE* f;

short 	level,		/*current {} nesting level*/ 
	skip_to_level,/*{} level to which parsing should skip (used to skip */ 
	              /*  font tables, style sheets, color tables, etc.)    */ 
	gobble,	/*Flag set to indicate all input should be discarded  */ 
	ignore_styles;/*Set to ignore inline style expansions after style use*/


char sTemplate[3][15]; /* string array used to store templates (e.g. http://, mailto:, ...).  The array lenght is hardcoded */
char sBuffer[200]; /* string buffer used to store hyperlinks while they are checked. Its length is hardcoded */
int iLength; /* length of the string buffer used to store hyperlinks */
int iTemplate; /* pointer to the template being currently checked. Holds -1 if no template is being checked */
int iNumTemplates = 3; /* the number of templates */
char sHref1[11];
char sHref2[3];
char sHref3[5]; /* string containing the html codes for hyperlinks */

/**************************************/

char RTF_GetChar()
{
	char ch;
	do{
		ch=fgetc(f);
	} while ((ch=='\r')||(ch=='\n'));
	return ch;
}

/**************************************/

char RTF_UnGetChar(char ch)
{
	return ungetc(ch,f);
}

/**************************************/

void RTF_PutStr(char* s)
{
	if (gobble) return;
	fputs(s, stdout);
}

/**************************************/

void RTF_PutHeader()
{
	RTF_PutStr("<HEAD>\n<TITLE>");
	RTF_PutStr(title);
	RTF_PutStr("</TITLE>\n</HEAD>\n");
}

/**************************************/

void RTF_PutChar(char ch)
{
	if (gobble) return;
	if(!isBody)
	{
		RTF_PutHeader();
		RTF_PutStr("<BODY>\n");
		isBody=TRUE;
	}
	switch (ch) {
		case '<':
			RTF_PutStr("&lt;");
			break;
			
		case '>':
			RTF_PutStr("&gt;");
			break;
			
		case '&':
			RTF_PutStr("&amp;");
			break;
		
		default:
			fputc(ch, stdout);
	}
}

/**************************************/

void RTF_PlainStyle (TStyleState* s)
{
	int i;
	for(i=0;i<MAX_INLINE_STYLES;i++)
	{
		if(s->s & (1<<i))
			RTF_PutStr(styles[i][1]);
	}
	s->s=0;
}

/**************************************/

void RTF_SetStyle(TStyleState* s, StyleState style)
{
	if( (!ignore_styles||(style==s_hidden)) && ((s->s&(1<<style))==0) )
	{
		RTF_PutStr(styles[style][0]);
		s->s|=(1<<style);
	}
}

/**************************************/

void RTF_PushState(short* level)
{
//	printf("<!--PushState=%X-->",curr_style.s);
	if(*level>=MAX_LEVELS)
	{
		fprintf(stderr,"Exceed maximum level\n");
		exit(-1);
	}
	style_state[*level]=curr_style;
	(*level)++;
}

/**************************************/

void RTF_PopState(short* level)
{
	int j;
	TStyleState new_style;
	
//	printf("<!--PopState=%X-->",curr_style.s);
	if(*level<1)
	{
		fprintf(stderr,"RTF parse error: unexpected '}'\n");
		exit(-1);
	}
	new_style = style_state[*level-1];
	/*close off any in-line styles*/
	for (j=0;j<MAX_INLINE_STYLES;j++) 
	{
		if ( ((curr_style.s & (1<<j))!=0) && ((new_style.s & (1<<j))==0) )
		{
			curr_style.s &= ~(1<<j);
			RTF_PutStr(styles[j][1]);
		}
	}
	
	for (j=0;j<MAX_INLINE_STYLES;j++) 
	{
		if( ((curr_style.s & (1<<j))==0) && ((new_style.s & (1<<j))!=0) )
			RTF_PutStr(styles[j][0]);
	}
	(*level)--;
	curr_style = new_style;

	if (*level == skip_to_level) {
		skip_to_level = -1;
		gobble = FALSE;
	}
}

/**************************************/
/* Map a style number into a HTML heading */

short RTF_MapStyle(char* s)
{
	int i;
	for (i=0;i<7;i++)
		if (!strcmp(style_mappings[i], s))
			return (i);
	return (0);
}

/**************************************/

void RTF_AddStyleMap(char* name, char* number)
{
	int i, len;
	len=strlen(name);
	if( name[len-1]==';') name[--len]=0;
	for(i=0;i<STYLE_NUMBER;i++)
	{
		if(!strcasecmp(name,style_namings[i]))
		{
			strcpy(style_mappings[i],number);
			return;
		}
	}
}

/**************************************/

void RTF_BuildName(char* token, char ch)
{
	int len;
	len = strlen(token);
	if(len>=MAX_NAME_LEN-1)
		return;
	token[len]=ch;
	token[len+1]=0;
}

/**************************************/

void RTF_ClearName(char* token)
{
	token[0]=0;
}

/**************************************/

TokenIndex GetTokenIndex(char* control)
{
	TokenIndex i;

	for (i=t_start; i<t_end; i++) 
	{
		if(control[0]==tokens[i][0]) /* Added for fast compare */
			if (!strcmp(control, tokens[i]))
				break;
	}
	return i;
}

/**************************************/

void RTF_DoStyleControl (char* control, char* arg)
{
	if(GetTokenIndex(control)==t_s)
	{
		strcpy(style_number,arg);
	}
}

/**************************************/

int chartoi(char ch)
{
	if((ch>='0')&&(ch<='9'))
		return ch-'0';
	if((ch>='A')&&(ch<='Z'))
		return ch-'A'+10;
	if((ch>='a')&&(ch<='z'))
		return ch-'a'+10;
	return -1;
}

/**************************************/

void RTF_BuildArg (char ch, char* arg)
{
	int i=0;

	if(feof(f))
	{
		arg[0]=0;
		return;
	}
	if(ch=='-')
	{
		arg[i++]='-';
		ch=RTF_GetChar();
		if(feof(f))
		{
			arg[0]=0;
			return;
		}
	}
	for(;isdigit(ch);i++)
	{
		arg[i]=ch;
		if(i>=MAX_RTF_TOKEN-1)
		{
			arg[MAX_RTF_TOKEN-1]=0;
			while(isdigit(ch)) {
				ch=RTF_GetChar();
				if(feof(f))
					return;
			} 
			break;
		}
		ch=RTF_GetChar();
		if(feof(f))
		{
			arg[i+1]=0;
			return;
		}
	}
	arg[i]=0;
	if(!isspace(ch))
		RTF_UnGetChar(ch);
}
	
/**************************************/

void RTF_BuildToken (char ch)
{
	int i;
	
	for(i=1;;i++)
	{
		char token[MAX_RTF_TOKEN], arg[MAX_RTF_TOKEN];
		token[i-1]=ch;
		if(i>=MAX_RTF_TOKEN-1)
		{
			do {
				ch=RTF_GetChar();
				if(feof(f))
					return;
			} while (isalpha(ch)); 	
			RTF_BuildArg(ch,arg);
			return;
		}
		ch=RTF_GetChar();
		if(feof(f))
		{
			token[i]=0;
			RTF_DoControl(token,"");
			return;
		}
		if( !isalpha(ch) )
		{
			token[i]=0;
			RTF_BuildArg(ch,arg);
			RTF_DoControl(token,arg);
			return;
		}
	}
}

/**************************************/

void RTF_backslash(char* pch, char* pf)
{
	char ch;
	*pf=FALSE;
	ch=RTF_GetChar();
	if(feof(f))
	{
		fprintf(stderr,"Unexpected end of file\n");
		return;
	}
	switch (ch) 
	{
		case '\\':
		case '{':
		case '}':
			*pch=ch; *pf=TRUE;
			break;
		case '*':
			gobble = TRUE;	/*perform no output, ignore commands 'til level-1*/
			if(skip_to_level>level-1||skip_to_level==-1) 
				skip_to_level = level-1;
			break;
		case '\'':
		{
			char ch1, ch2;
			ch1 = RTF_GetChar();
			ch2 = RTF_GetChar();
			if(!feof(f)) 
			{
				if(isxdigit(ch1)&&isxdigit(ch2))
				{
					ch = chartoi(ch1)*16+chartoi(ch2);
					*pch=ch; *pf=TRUE;
				} else {
					fprintf(stderr,"RTF Error: unexpected '%c%c' after \\\'\n",ch1,ch2);
				}
			}
			break;
		}
		default:
			if (isalpha(ch)) 
			{
				RTF_BuildToken(ch);
			} else {
				fprintf(stderr, "\nRTF Error: unexpected '%c' after \\.\n", ch);
			}
			break;
	}
}

/**************************************/

void RTF_ParseStyle()
{
	char ch, pf;
	int level0;
	void (*PrevDoControl)(char*,char*);
	
	level0=level;
	PrevDoControl=RTF_DoControl;
	RTF_DoControl=RTF_DoStyleControl;
	
	RTF_ClearName(style_name);
	style_number[0]=0;
	while (1) 
	{
		ch = RTF_GetChar();
		if(feof(f))
			break;
		switch (ch) 
		{
			case '\\':
				RTF_backslash(&ch,&pf);
				if(pf)
				{
					RTF_BuildName(style_name,ch);
				} else {
					RTF_ClearName(style_name);
				}
				break;
			
			case '{':
				level++;
				RTF_ClearName(style_name);
				break;
			
			case '}':
				if(level0+1==level)
				{
					if(style_number[0]!=0)
					{
						RTF_AddStyleMap(style_name,style_number);
						style_number[0]=0;
					}
				} else if(level0==level) {
					RTF_DoControl=PrevDoControl;
					RTF_UnGetChar(ch);
					return;
				}
				level--;
				RTF_ClearName(style_name); 
				break;
				
			default:
				RTF_BuildName(style_name,ch);
				break;
		}
	} /* while */
}

/**************************************/
/* Perform actions for RTF control words */

void RTF_DoBodyControl (char* control,char* arg)
{
	short style;

	if (gobble) return;

	switch (GetTokenIndex(control)) 
	{
		case t_stylesheet:
			gobble = TRUE;	/*perform no output, ignore commands 'til level-1*/
			skip_to_level = level-1;
			RTF_ParseStyle();
			break;
		case t_fonttbl:	/*skip all of these and their contents!*/
		case t_colortbl:
		case t_info:
			gobble = TRUE;	/*perform no output, ignore commands 'til level-1*/
			skip_to_level = level-1;
			break;
		case t_pict:
			gobble = TRUE;	/*perform no output, ignore commands 'til level-1*/
			if(skip_to_level>=level || skip_to_level==-1) 
				skip_to_level = level-1;
			break;
			
			
		case t_s: /*Style*/
			if (!curr_heading) 
			{
				style = RTF_MapStyle (arg);
				if(style)
				{
					curr_heading = s_h0 + style;
					RTF_PutStr(styles[curr_heading][0]);
					ignore_styles = TRUE;
				}
			}
			break;
			
		case t_b: /*Bold*/
				RTF_SetStyle(&curr_style,s_bold);
			break;
			
		case t_ulw:
		case t_uld:
		case t_uldb:
		case t_ul: /*Underline, maps to "emphasis" HTML style*/
				RTF_SetStyle(&curr_style,s_underline);
			break;
			
		case t_i: /*Italic*/
				RTF_SetStyle(&curr_style,s_italic);
			break;
			
		case t_v: /* Hidden*/
				RTF_SetStyle(&curr_style,s_hidden);
			break;
			
		case t_par: /*Paragraph*/
			if (curr_heading!=s_plain) {
				RTF_PutStr(styles[curr_heading][1]);
				curr_heading = s_plain;
			} else {
				RTF_PutStr(styles[s_para][0]);
			}
			ignore_styles = FALSE;
			break;
			
		case t_plain: /*reset inline styles*/
			RTF_PlainStyle(&curr_style);
			break;
		case t_cell:
		case t_tab:
			RTF_PutChar(' ');
			break;
		case t_endash:
		case t_emdash:
			RTF_PutChar('-');
			break;
		case t_line:
		case t_row:
			RTF_PutStr(styles[s_br][0]);

			break;
		case t_bullet:
			RTF_PutChar('\xb7');
			break;
		case t_start:
		case t_end:
			break;
	}
			
}

/**************************************/
/* RTF_Parse is a crude, ugly state machine that understands enough of */
/* the RTF syntax to be dangerous.                                     */

void RTF_ParseBody()
{
	char ch, pf;
	int iCurrTemplate;

	RTF_DoControl=RTF_DoBodyControl;
	level = 0;
	skip_to_level = -1;
	gobble = FALSE;
	ignore_styles = FALSE;
	
	while (1) 
	{
		ch = RTF_GetChar();
		if(feof(f))
			break;
		switch (ch) 
		{
			case '\\':
				RTF_backslash(&ch,&pf);
				if(pf) RTF_PutChar(ch);
				break;
			
			case '{':
				RTF_PushState(&level);
				break;
			
			case '}':
				RTF_PopState(&level);
				break;
				
			default:
				/* RTF_PutChar(ch); */
				if(iTemplate == -1) /* no any template is being checked */
				{
					for(iCurrTemplate=0;iCurrTemplate<iNumTemplates;iCurrTemplate++)
					{
						if(ch == sTemplate[iCurrTemplate][iLength]) /* the iCurrTemplate-th template matches */
						{
							iTemplate = iCurrTemplate;
							sBuffer[iLength++] = ch;
							break;
						}
					}
					if(iTemplate == -1) /* still no any template is being checked */
						RTF_PutChar(ch);
				}
				else /* the template pointed by iTemplate is being checked */
				{
					if(sTemplate[iTemplate][iLength] == ch) /* the template still matches */
					{
						sBuffer[iLength++] = ch; /* stores the read char and update the buffer length */
						if(iLength == strlen(sTemplate[iTemplate])) /* the template completely matches */
						{
							ch = RTF_GetChar();
							while((!isspace(ch)) && (ch!='<') && (ch!='\\') && (ch!='{') && (ch!='}')) /* keeps on adding chars to the hyperlik */
							{					/* unless a space, a newline or a < is foud */
								sBuffer[iLength++] = ch;
								ch = RTF_GetChar();
							}
							sBuffer[iLength+1] = '\0';
							RTF_PutStr(sHref1);
							RTF_PutStr(sBuffer);
							RTF_PutStr(sHref2);
							RTF_PutStr(sBuffer);
							RTF_PutStr(sHref3);
							RTF_UnGetChar(ch);
							iTemplate = -1;
							iLength = 0;
						}
					}
					else /* the template does not match */
					{
						sBuffer[iLength] = ch;
						sBuffer[iLength+1] = '\0';
						RTF_PutStr(sBuffer); /* output thestored text and reset variables */
						iTemplate = -1;
						iLength = 0;
					}
				}
				break;
		}
	}/*while*/
}

/**************************************/

int RTF_Parse (char* filename)
{
	if (filename) 
	{
		if (!(f = fopen (filename, "r"))) 
		{
			fprintf (stderr, "\nError: Input file %s not found.\n", filename);
			return (-1);
		}
		title=filename;
	} else {
		f = stdin;
		title="STDIN";
	}
		
	RTF_PutStr("<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n<HTML>\n");
	isBody=FALSE;
	RTF_ParseBody(f);
	if(isBody) RTF_PutStr("</BODY>\n");
	RTF_PutStr("</HTML>\n");

	fclose (f);
	return 0;
}

/**************************************/

void Initialize()
{
	int i;

	for (i=0;i<MAX_LEVELS;i++)
			style_state[i].s=s_plain;

	curr_style.s=s_plain;
	curr_heading = s_plain;

	// Set default styles maping
	style_mappings[0][0]=0;
	for(i=1;i<=6;i++)
			sprintf(style_mappings[i],"%d",256-i);

	iLength = 0;
	iTemplate = -1;
	strcpy(sHref1,"<A HREF='");
	strcpy(sHref2,"'>");
	strcpy(sHref3,"</A>");
	strcpy(sTemplate[0],"http://");
	strcpy(sTemplate[1],"ftp://");
	strcpy(sTemplate[2],"mailto:");
}

/**************************************/

int main(int argc,char** argv)
{

	Initialize();
	
	if (argc>1)
	{
		if( strcmp(argv[1],"--help")==0 || strcmp(argv[1],"-H")==0 )
		{
			printf("Use: %s [rtf_filename]\n",argv[0]);
			return 0;
		} else if ( strcmp(argv[1],"--version")==0 || strcmp(argv[1],"-V")==0 ) {
			printf("rtf2html version 1.1 beta\n");
			return 0;
		}
	}
			
	if (argc>1)
		return (RTF_Parse(argv[1]));
	else
		return (RTF_Parse(NULL));
}
