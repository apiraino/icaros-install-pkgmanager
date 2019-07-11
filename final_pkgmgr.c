/*
 * pkgmgr.c
 *
 *	Created on: Sep 11, 2012
 *  	Author: jman
 *
 * 	Copyright (C) 2012, Antonio Piraino. All rights reserved.
 *	$Id: pkgmgr.c 123 2012-11-09 19:16:17Z jman $
 *
 *   Desc: Package Selector for Icaros installation
 *   Lang: english
 *
 *      License: http://aros.sourceforge.net/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/mui.h>

#include <SDI/SDI_hook.h>

#define DEBUG 0
#include <aros/debug.h>

#define VERSTAG	"\0$VER: Icaros package installer 0.3 (25.9.2012)"

// Path to retrieve packages
#define PKGPATH	"ENV:icaros/icinstall/"

// Path to retrieve packages description files
#define PKGDESC	"Extras:"
#define MAX_PKGDESC_CONTENT 640 // 640 bytes ought to be enough for anybody ^_^

typedef enum
{
	EXIT_OK		= EXIT_SUCCESS,
	EXIT_ERR	= EXIT_FAILURE
} exit_t;

struct listItems
{
	char *_filename;
	char *_pkgdesc;
	int _value;
	struct listItems *_next;
};

struct listCheckboxes
{
	Object *item;
	struct listCheckboxes *next;
};

struct listItems *globalList, *save_globalList;
struct listCheckboxes *globalListCheckboxes, *save_globalListCheckboxes;

void freeAll(void)
{
	// free(globalList); globalList = NULL;
	free(save_globalList); save_globalList = NULL;
	free(globalListCheckboxes); globalListCheckboxes = NULL;
	free(save_globalListCheckboxes); save_globalListCheckboxes = NULL;
}

Object *MakeCheck(BYTE state, char *label, char *desc)
{
  return ImageObject,
           ImageButtonFrame,
           MUIA_ObjectID, label,
           MUIA_Text_Contents, desc,
           MUIA_InputMode   , MUIV_InputMode_Toggle,
           MUIA_Image_Spec  , MUII_CheckMark,
           MUIA_Background  , MUII_ButtonBack,
           MUIA_ShowSelState, state,
           // MUIA_ControlChar , ShortCut(label),
           MUIA_CycleChain  , TRUE,
         End;
}

exit_t retrieveDesc(FILE *fp, char *fname, char **ris)
{
	char *pkgdesc;
	char pkgdesc_content[MAX_PKGDESC_CONTENT];
	pkgdesc_content[0]	= '\0';

	// Retrieve the description file
								// "Extras:/" + ep->d_name + ".desc" + 1
	if (NULL == (pkgdesc = malloc( strlen(PKGDESC) + strlen(fname) + 4 + 1 * sizeof(char) )))
	{
		printf("Error allocating memory!\n");
		return EXIT_FAILURE;
	}
	pkgdesc[0] = '\0';

	strncat(pkgdesc, PKGDESC, strlen(PKGDESC));
	strncat(pkgdesc, fname, strlen(fname));
	strncat(pkgdesc, ".desc", strlen(".desc"));

	// Trim newline
	if (pkgdesc[strlen(pkgdesc) - 1] == '\n')
		pkgdesc[strlen(pkgdesc) - 1] = '\0';

	if (-1 != access(pkgdesc, F_OK|R_OK))
	{
		if (NULL == (fp = fopen(pkgdesc, "r")))
		{
			printf("Failed opening file %s, error: %d\n", pkgdesc, errno);
			return EXIT_FAILURE;
		}

		char line[128];
		line[0]	= '\0';
		while(NULL != fgets(line, sizeof(line), fp))
		{
			if (';' == line[0])
				continue;
			strncat(pkgdesc_content, line, strlen(line));
		}

		fclose(fp); fp = NULL;

		if (NULL == (*ris = malloc( strlen(pkgdesc_content) * sizeof(char *))))
		{
			printf("malloc failed\n");
			return EXIT_FAILURE;
		}
		*ris	= pkgdesc_content;
	}
	else
	{
		// Could not find .desc file, will put the filename instead
		*ris	= malloc( strlen(fname) * sizeof(char *));
		*ris	= fname;
	}

	pkgdesc	= NULL; free(pkgdesc);
	return EXIT_SUCCESS;
}

void bubble_sort(struct listItems *list)
{
	int done = 0;
	struct listItems *n;
	struct listItems *head = list;
	char *tmp;
	while ( ! done )
	{
	    done = 1;
	    n = head;
	    while ( n->_next )
	    {
	    	if (strcasecmp(n->_filename, n->_next->_filename) > 0)
	        {
	             done = 0;
	             // swap the data
	             tmp = n->_filename;
	             n->_filename = n->_next->_filename;
	             n->_next->_filename = tmp;
	        }
	        n = n->_next;
	    }
	}

}

exit_t populateList(char *path, Object *grp)
{
	int ris = EXIT_FAILURE;

	DIR *dp;
	struct dirent *ep;
	FILE *fp;
	char str[100];
	char *filename;

	struct listItems *file_list;
	// Allocate for the filenames list
	if (NULL == (file_list = malloc(sizeof(struct listItems))))
	{
		printf("Error allocating memory for list!\n");
		return EXIT_FAILURE;
	}
	file_list->_filename	= NULL;
	file_list->_next		= NULL;
	struct listItems *w		= file_list;
	char *_true				= "True"; // expected content of file in order to tick the checkbox
	char *_tmpdesc;

	errno = 0;
	dp = opendir ((char *)path);

	if (0 != errno)
	{
		char *errmsg;
		switch(errno)
		{
			case EACCES:	errmsg	= "Permission denied."; break;
			case EBADF:		errmsg	= "fd is not a valid file descriptor opened for reading."; break;
			case EMFILE:	errmsg	= "Too many file descriptors in use by process."; break;
			case ENFILE:	errmsg	= "Too many files are currently open in the system."; break;
			case ENOENT:	errmsg	= "Directory does not exist, or name is an empty string."; break;
			case ENOMEM:	errmsg	= "Insufficient memory to complete the operation."; break;
			case ENOTDIR:	errmsg	= "name is not a directory."; break;
			// default considered harmful
		}

		printf("Could not open directory:\n%s\n", errmsg);
		return EXIT_FAILURE;
	}

	if (dp != NULL)
	{
		// scan dir
		while (NULL != (ep = readdir (dp)))
		{
			// printf("name=%s, reclen=%d, type=%d\n", ep->d_name, ep->d_reclen, ep->d_type);
			// If file is a regular file, open and read content
			if (DT_REG == ep->d_type)
			{
				int l1 = strlen(path);
				int l2 = strlen(ep->d_name);
				if (NULL == (filename = malloc( l1 + 1 + l2 + 1 * sizeof(char) )))
				{
					printf("Error allocating memory!\n");
					return EXIT_FAILURE;
				}
				filename[0] = '\0';

				strncat(filename, path, strlen(path));
				strncat(filename, ep->d_name, strlen(ep->d_name));
				// Is file accessible?
				if (-1 != access(filename, F_OK|R_OK|W_OK))
				{
 					if (NULL == (fp = fopen(filename, "r")))
					{
						printf("Failed opening file %s, error: %d\n", filename, errno);
						return EXIT_FAILURE;
					}

 					int s = strlen(filename);
					if (NULL == fgets(str, s, fp))
					{
						printf("Failed reading file '%s', error: %d\n", filename, errno);
						fclose(fp); fp = NULL;
						return EXIT_FAILURE;
					}

					// Trim newline
					if (str[strlen(str) - 1] == '\n')
						str[strlen(str) - 1] = '\0';

					fclose(fp); fp = NULL;
					fp = NULL;

					if (EXIT_FAILURE == retrieveDesc(fp, ep->d_name, &_tmpdesc))
					{
						printf("Bad error occurred while retrieving package description\n");
						return EXIT_FAILURE;
					}

					// List is not empty, allocate and populate an additional item
					if (NULL != file_list->_filename)
					{
						struct listItems *l;
						l = malloc(sizeof(struct listItems));
						l->_filename	= strdup(ep->d_name);

						if (0 == strncmp(_true, str, strlen(str)))
							l->_value	= 1;
						else
							l->_value	= 0;

						if (0 != strlen(_tmpdesc))
							l->_pkgdesc		= strdup(_tmpdesc);
						else
							l->_pkgdesc		= l->_filename;

						file_list->_next	= l;
						l->_next			= NULL;
						file_list			= file_list->_next;
					}
					else
					{
						// List is empty, populate first item
						file_list->_filename	= strdup(ep->d_name);
						file_list->_pkgdesc		= strdup(_tmpdesc);
						if (0 == strncmp(_true, str, strlen(str)))
							file_list->_value	= 1;
						else
							file_list->_value	= 0;
					}

				}
				else
				{
					printf("File %s could not be: found, read or written\n", filename);
				}
			}
		}
		ris	= EXIT_SUCCESS;
		(void) closedir (dp);
	}
	else
		printf("Couldn't open the directory");


	// For each entry tick/untick the checkbox

	// TODO sort the linked list
	// Not much use on a second thought. I may be showing a mix of filenames and descriptions
	// (impossible to sort)
	// bubble_sort(w);

	if(DoMethod(grp, MUIM_Group_InitChange))
	{
		while(NULL != w)
		{
			// TODO Retrieve useful data for checkboxes from an associate MUIA_UserData pointer

			Object *child	= MakeCheck(FALSE, w->_filename, w->_pkgdesc);
			set(child, MUIA_ObjectID, w->_filename);

			Object *_row = ColGroup(3), GroupFrame,
					Child, child,
					Child, MUI_MakeObject(MUIO_HSpace, 20),
					Child, MUI_NewObject(MUIC_Text,MUIA_ObjectID, "pkgdesc", MUIA_Text_Contents, w->_pkgdesc),
					Child, MUI_MakeObject(MUIO_VSpace, 10),
				End;
			DoMethod(grp, OM_ADDMEMBER, _row);

			DoMethod(grp, MUIM_Group_ExitChange);

			if (w->_value)
				set(child, MUIA_Selected, TRUE);

			// Save the checkbox in a global structure for future retrieval
			// without having to iterate through dozen of nested groups

			if (NULL == globalListCheckboxes->item)
			{
//				printf("Adding content (%s,%d) to list=0x%p\n",
//							XGET(child, MUIA_ObjectID),
//							XGET(child, MUIA_Selected),
//							globalListCheckboxes);

				globalListCheckboxes->item	= child;
			}
			else
			{
				struct listCheckboxes *new;
				if (NULL == (new = malloc(sizeof(struct listCheckboxes))))
				{
					printf("Error allocating memory!\n");
					return EXIT_FAILURE;
				}

				new->item	= child;
				new->next	= NULL;

//				printf("Adding new item (%s,%d) to list=0x%p -> 0x%p\n",
//							XGET(new->item, MUIA_ObjectID),
//							XGET(new->item, MUIA_Selected),
//							globalListCheckboxes,
//							globalListCheckboxes->next);

				globalListCheckboxes->next	= new;
				globalListCheckboxes		= globalListCheckboxes->next;
			}

			w	= w->_next;
		}
	}
	else
	{
		printf("Problem editing group %s\n", (char *)XGET(grp, MUIA_ObjectID));
		return EXIT_FAILURE;
	}

	free(file_list); file_list = NULL;
	free(w); w = NULL;
	free(filename); filename = NULL;
	return ris;
}

HOOKPROTONHNO(saveSetupClick, void, APTR *data)
{
	Object *_win	= *data++;
	Object *_app	= *data;

	char *file_path;
	char *filename;
	int isSelected;
	FILE *fp;
	Object *item;

	struct listCheckboxes *items	= save_globalListCheckboxes;
	while (NULL != items)
	{
		item	= items->item;
		// printf("0x%p filename=%s, value=%d\n", _tmp, _tmp->_filename, _tmp->_value);
		// printf("0x%p, %s,%d\n", item, XGET(item, MUIA_ObjectID), XGET(item, MUIA_Selected));

		if (NULL != (char *)XGET(item, MUIA_ObjectID))
		{
			filename	= (char *)XGET(item, MUIA_ObjectID);
			isSelected	= XGET(item, MUIA_Selected);

			file_path		= malloc(strlen(filename)+strlen(PKGPATH)+1 * sizeof(char));
			file_path[0] 	= '\0';
			strncat(file_path, PKGPATH, strlen(PKGPATH));
			strncat(file_path, filename, strlen(filename));

			errno	= 0;
			if (NULL == (fp = fopen(file_path, "w")))
			{
				printf("Failed opening file %s, error: %d\n", filename, errno);
			}
			else
			{
				if (1 == (int)isSelected)
					fprintf(fp, "True");
				else
					fprintf(fp, "False");
				fclose(fp); fp = NULL;
			}

			file_path	= NULL; free(file_path);
			fp = NULL;
		}

		items	= items->next;
	}

    // Once finished, bail out.
	SetAttrs(_win, MUIA_Window_Open, FALSE, TAG_DONE);
	MUI_DisposeObject(_app);

	freeAll();
	// TODO shall check leftover in memory
	exit(0);
}
MakeHook(saveSetupHook, &saveSetupClick);

int main(VOID)
{
	STRPTR desc	= (STRPTR)"Welcome to Icaros package selector.\n"
			"Please select desired components for installation, and click on Continue";

	Object *window, *app;
	Object *mainGrp, *itemsGrp, *continueBtn;
	Object *selectAll, *selectNone;

	Object *virtGrp	= MUI_NewObject(MUIC_Virtgroup,

					Child, MUI_NewObject(MUIC_Text,
						MUIA_Text_PreParse, "\33c",
						StringFrame,
						// MUIA_Text_SetVMax, FALSE,
						MUIA_Text_Contents, desc,
					TAG_DONE),

					Child, MUI_MakeObject(MUIO_VSpace,10),

					Child, ColGroup(2), GroupFrame,
						Child, selectAll 	= SimpleButton("Select all"),
						Child, selectNone	= SimpleButton("Select none"),
					End,

					Child, itemsGrp = ColGroup(1), GroupFrame,
						MUIA_ObjectID, "ItemList",
						// This group will be populated when the application starts
						// and scans for the desired directory
					End,

					Child, MUI_MakeObject(MUIO_VSpace,10),

					TAG_DONE);

	mainGrp	=	MUI_NewObject(MUIC_Scrollgroup,
					MUIA_ObjectID, "ID_mainGrp",
					// Does it exist on AROS?
					// MUIA_Scrollgroup_AutoBars, TRUE,
					MUIA_Scrollgroup_Contents, virtGrp,
					Child, continueBtn = SimpleButton("Continue"),
					TAG_DONE);

	window	= MUI_NewObject(MUIC_Window,
					MUIA_Window_Height, MUIV_Window_Height_Visible(85),
					MUIA_Window_Width, MUIV_Window_Width_Visible(85),
					MUIA_Window_Title, "Icaros package installer",
					MUIA_Window_ID, "ID_window",
					MUIA_Window_RootObject, mainGrp,
					TAG_DONE);

	app	= MUI_NewObject(MUIC_Application,
					MUIA_Application_Title, "Icaros package installer",
					MUIA_Application_Description, "Icaros package installer",
					MUIA_Application_Base, "Application",
					MUIA_Application_Window, window,
					MUIA_Application_Version, VERSTAG,
					TAG_DONE);

	SetAttrs(app, OM_ADDMEMBER, window);

#if DEBUG
	// Automatic application exit disabled
	DoMethod(window, MUIM_Notify,
			MUIA_Window_CloseRequest, TRUE,
			app,
			2,
			MUIM_Application_ReturnID,
			MUIV_Application_ReturnID_Quit);
#endif

	DoMethod(continueBtn, MUIM_Notify,
			MUIA_Pressed, FALSE,
			MUIV_Notify_Application,
			4,
			MUIM_CallHook,
			&saveSetupHook,
			window, app);

	DoMethod(selectAll, MUIM_Notify,
			MUIA_Pressed, FALSE,
			itemsGrp,
			3,
			MUIM_Set,
			MUIA_Selected, TRUE);

	DoMethod(selectNone, MUIM_Notify,
			MUIA_Pressed, FALSE,
			itemsGrp,
			3,
			MUIM_Set,
			MUIA_Selected, FALSE);


	if (NULL == (globalListCheckboxes = malloc(sizeof(struct listCheckboxes))))
	{
		printf("Error allocating memory!\n");
		return EXIT_FAILURE;
	}
	globalListCheckboxes->item	= NULL;
	globalListCheckboxes->next	= NULL;
	save_globalListCheckboxes	= globalListCheckboxes;

	if (NULL == (globalList = malloc(sizeof(struct listItems))))
	{
		printf("Error allocating memory!\n");
		return EXIT_FAILURE;
	}
	globalList->_filename	= NULL;
	globalList->_pkgdesc	= NULL;
	globalList->_value		= 0;
	globalList->_next		= NULL;

	// Save starting address of struct
	save_globalList	= globalList;

	if (EXIT_SUCCESS != populateList(PKGPATH, itemsGrp))
	{
		printf("Error parsing directory: %s\n", PKGPATH);
		printf("Press ENTER to bail out...\n");
		gets("");

		SetAttrs(window, MUIA_Window_Open, FALSE, TAG_DONE);
		MUI_DisposeObject(app);
		return EXIT_FAILURE;
	}

	SetAttrs(window, MUIA_Window_Open, TRUE, TAG_DONE);

	if (NULL != app)
	{
		DoMethod(app, MUIM_Application_Execute);
		SetAttrs(window, MUIA_Window_Open, FALSE, TAG_DONE);
		MUI_DisposeObject(app);
	}

	freeAll();
	return EXIT_SUCCESS;
}
