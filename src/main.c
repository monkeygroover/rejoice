#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "globals.h"

#ifdef GC_BDW
#include "gc/gc.h"
#endif

#include "utils.c"
#include "scan.c"
#include "interp.c"

static void enterglobal()
{
        location = symtabindex++;
        D(printf("getsym, new: '%s'\n", id));
#ifdef GC_BDW
        location->name = (char *)GC_malloc_atomic(strlen(id) + 1);
#else
        location->name = (char *)malloc(strlen(id) + 1);
#endif
        strcpy(location->name, id);
        location->u.body = NULL; /* may be assigned in definition */
        location->next = hashentry[hashvalue];
        D(printf("entered %s at %ld\n", id, LOC2INT(location)));
        hashentry[hashvalue] = location;
}

void lookup()
{
        int i;
        D(printf("%s  hashes to %d\n", id, hashvalue));
        for (i = display_lookup; i > 0; --i)
        {
                location = display[i];
                while (location != NULL && strcmp(id, location->name) != 0)
                {
                        location = location->next;
                }
                if (location != NULL) /* found in local table */
                {
                        return;
                }
        }
        location = hashentry[hashvalue];
        while (location != symtab && strcmp(id, location->name) != 0)
        {
                location = location->next;
        }
        if (location == symtab) /* not found, enter in global */
        {
                enterglobal();
        }
}

static void enteratom()
{
        if (display_enter > 0)
        {
                location = symtabindex++;
                D(printf("hidden definition '%s' at %ld \n", id,
                         LOC2INT(location)));
#ifdef GC_BDW
                location->name = (char *)GC_malloc_atomic(strlen(id) + 1);
#else
                location->name = (char *)malloc(strlen(id) + 1);
#endif
                strcpy(location->name, id);
                location->u.body = NULL; /* may be assigned later */
                location->next = display[display_enter];
                display[display_enter] = location;
        }
        else
        {
                lookup();
        }
}

static void defsequence();

static void compound_def();

static void definition()
{
        Entry *here = NULL;
        if (sym == LIBRA || sym == PRIVATE || sym == HIDE || sym == MODULE)
        {
                compound_def();
                if (sym == END || sym == PERIOD)
                {
                        getsym();
                }
                else
                {
                        error(" END or period '.' expected in compound "
                              "definition");
                }
                return;
        }
        if (sym != ATOM)
        {
                // NOW ALLOW EMPTY DEFINITION:
                // error("atom expected at start of definition");
                // abortexecution_();
                return;
        }
        /* sym == ATOM : */
        enteratom();
        if (location < firstlibra)
        {
                printf("warning: overwriting inbuilt '%s'\n", location->name);
                enterglobal();
        }
        here = location;
        getsym();
        if (sym == EQDEF)
        {
                getsym();
        }
        else
        {
                error(" == expected in definition");
        }
        readterm();
        D(printf("assigned this body: "));
        D(writeterm(stk->u.lis, stdout));
        D(printf("\n"));
        if (here != NULL)
        {
                here->u.body = stk->u.lis;
                here->is_module = 0;
        }
        stk = stk->next;
}

static void defsequence()
{
        definition();
        while (sym == SEMICOL)
        {
                getsym();
                definition();
        }
}

static void compound_def()
{
        switch (sym)
        {
                case MODULE:
                {
                        Entry *here = NULL;
                        getsym();
                        if (sym != ATOM)
                        {
                                error("atom expected as name of module");
                                abortexecution_();
                        }
                        enteratom();
                        here = location;
                        getsym();
                        ++display_enter;
                        ++display_lookup;
                        display[display_enter] = NULL;
                        compound_def();
                        here->is_module = 1;
                        here->u.module_fields = display[display_enter];
                        --display_enter;
                        --display_lookup;
                        break;
                }
                case PRIVATE:
                case HIDE:
                {
                        getsym();
                        if (display_lookup > display_enter)
                        {
                                /* already inside module or hide */
                                Entry *oldplace = display[display_lookup];
                                // printf("lookup =
                                // %d\n",LOC2INT(display[display_lookup]));
                                // printf("enter =
                                // %d\n",LOC2INT(display[display_enter]));
                                ++display_enter;
                                defsequence();
                                --display_enter;
                                // printf("lookup =
                                // %d\n",LOC2INT(display[display_lookup]));
                                // printf("enter =
                                // %d\n",LOC2INT(display[display_enter]));
                                compound_def();
                                display[display_lookup] = oldplace;
                        }
                        else
                        {
                                ++display_enter;
                                ++display_lookup;
                                display[display_enter] = NULL;
                                defsequence();
                                --display_enter;
                                compound_def();
                                --display_lookup;
                        }
                        break;
                }
                case PUBLIC:
                case LIBRA:
                case IN:
                {
                        getsym();
                        defsequence();
                        break;
                }
                default:
                {
                        printf("warning: empty compound definition\n");
                }
        }
}

jmp_buf begin;
jmp_buf fail;

void abortexecution_()
{
        conts = dump = dump1 = dump2 = dump3 = dump4 = dump5 = NULL;
        longjmp(begin, 0);
}

void fail_()
{
        longjmp(fail, 1);
}

void execerror(char *message, char *op)
{
        printf("run time error: %s needed for %s\n", message, op);
        abortexecution_();
}

/* was = 0;  but anything with "clock" needs revision */
static int quit_quiet = 1;

void quit_()
{
        long totaltime;
        if (quit_quiet)
        {
                exit(0);
        }
        totaltime = clock() - startclock;
#ifdef GC_BDW
        printf("Time:  %ld CPU\n", totaltime);
#else
        printf("Time:  %ld CPU,  %d gc (= %ld%%)\n", totaltime, gc_clock,
               totaltime ? (1004 * gc_clock) / (10 * totaltime) : 0);
#endif
        exit(0);
}

static int mustinclude = 1;

#define CHECK(D, NAME)                                                         \
        if (D)                                                                 \
        {                                                                      \
                printf("->  %s is not empty:\n", NAME);                        \
                writeterm(D, stdout);                                          \
                printf("\n");                                                  \
        }

int main(int argc, char **argv)
{
        int ch;
        g_argc = argc;
        g_argv = argv;
        if (argc > 1)
        {
                g_argc--;
                g_argv++;
                srcfile = fopen(argv[1], "r");
                if (!srcfile)
                {
                        printf("failed to open the file '%s'.\n", argv[1]);
                        exit(1);
                }
        }
        else
        {
                srcfile = stdin;
#ifdef GC_BDW
                printf("Rejoice 0.0.1 GC=BDW\n");
#else
                printf("Rejoice 0.0.1 GC=Custom\n");
#endif
        }
#ifdef GC_BDW
        GC_INIT();
#endif
        startclock = clock();
        gc_clock = 0;
        echoflag = INIECHOFLAG;
        tracegc = INITRACEGC;
        autoput = INIAUTOPUT;
        ch = ' ';
        inilinebuffer();
        inisymboltable();
        display[0] = NULL;
        inimem1();
        inimem2();
        setjmp(begin);
        setjmp(fail);
        D(printf("starting main loop\n"));
        while (1)
        {
                if (mustinclude)
                {
                        mustinclude = 0;
                        if (fopen("usrlib.joy", "r"))
                        {
                                doinclude("usrlib.joy");
                        }
                }
                getsym();
                if (sym == LIBRA || sym == HIDE || sym == MODULE)
                {
                        inimem1();
                        compound_def();
                        inimem2();
                }
                else
                {
                        readterm();
                        D(printf("program is: "); writeterm(stk->u.lis, stdout);
                          printf("\n"));
                        prog = stk->u.lis;
                        stk = stk->next;
                        conts = NULL;
                        exeterm(prog);
                        if (conts || dump || dump1 || dump2 || dump3 || dump4
                            || dump5)
                        {
                                printf("the dumps are not empty\n");
                                CHECK(conts, "conts");
                                CHECK(dump, "dump");
                                CHECK(dump1, "dump1");
                                CHECK(dump2, "dump2");
                                CHECK(dump3, "dump3");
                                CHECK(dump4, "dump4");
                                CHECK(dump5, "dump5");
                        }
                        if (autoput == 2 && stk != NULL)
                        {
                                writeterm(stk, stdout);
                                printf("\n");
                        }
                        else if (autoput == 1 && stk != NULL)
                        {
                                writefactor(stk, stdout);
                                printf("\n");
                                stk = stk->next;
                        }
                }
                if (sym != END && sym != PERIOD)
                {
                        error(" END or period '.' expected");
                }
        }
}
