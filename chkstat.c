/* Copyright (c) 2004 SuSE Linux AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 ****************************************************************
 */

#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __USE_GNU
#include <fcntl.h>


struct perm {
  struct perm *next;
  char *file;
  char *owner;
  char *group;
  mode_t mode;
};

struct perm *permlist;
char **checklist;
int nchecklist;
uid_t euid;
char *root;
int rootl;

void
add_permlist(char *file, char *owner, char *group, mode_t mode)
{
  struct perm *ec, **epp;

  owner = strdup(owner);
  group = strdup(group);
  if (rootl)
    {
      char *nfile;
      nfile = malloc(strlen(file) + rootl + (*file != '/' ? 2 : 1));
      if (nfile)
	{
	  strcpy(nfile, root);
	  if (*file != '/')
	    strcat(nfile, "/");
	  strcat(nfile, file);
	}
      file = nfile;
    }
  else
    file = strdup(file);
  if (!owner || !group || !file)
    {
      perror("permlist entry alloc");
      exit(1);
    }
  for (epp = &permlist; (ec = *epp) != 0; )
    if (!strcmp(ec->file, file))
      {
        *epp = ec->next;
        free(ec->file);
        free(ec->owner);
        free(ec->group);
        free(ec);
      }
    else
      epp = &ec->next;
  ec = malloc(sizeof(struct perm));
  if (ec == 0)
    {
      perror("permlist entry alloc");
      exit(1);
    }
  ec->file = file;
  ec->owner = owner;
  ec->group = group;
  ec->mode = mode;
  ec->next = 0;
  *epp = ec;
}

int
in_checklist(char *e)
{
  int i;
  for (i = 0; i < nchecklist; i++)
    if (!strcmp(e, checklist[i]))
      return 1;
  return 0;
}

void
add_checklist(char *e)
{
  if (in_checklist(e))
    return;
  e = strdup(e);
  if (e == 0)
    {
      perror("checklist entry alloc");
      exit(1);
    }
  if ((nchecklist & 63) == 0)
    {
      if (checklist == 0)
	checklist = malloc(sizeof(char *) * (nchecklist + 64));
      else
	checklist = realloc(checklist, sizeof(char *) * (nchecklist + 64));
      if (checklist == 0)
	{
	  perror("checklist alloc");
	  exit(1);
	}
    }
  checklist[nchecklist++] = e;
}

int
readline(FILE *fp, char *buf, int len)
{
  int l;
  if (!fgets(buf, len, fp))
    return 0;
  l = strlen(buf);
  if (l && buf[l - 1] == '\n')
    {
      l--;
      buf[l] = 0;
    }
  if (l + 1 < len)
    return 1;
  fprintf(stderr, "warning: buffer overrun in line starting with '%s'\n", buf);
  while ((l = getc(fp)) != EOF && l != '\n')
    ;
  buf[0] = 0;
  return 1;
}

void
usage(int x)
{
  fprintf(stderr, "Usage: chkstat [--set] [--noheader] [[--examine file] ...] [ [--files filelist] ...] permission-file ...\n");
  exit(x);
}

int
safepath(char *path, uid_t uid, gid_t gid)
{
  struct stat stb;
  char pathbuf[1024];
  char linkbuf[1024];
  char *p, *p2;
  int l, l2, lcnt;

  lcnt = 0;
  l2 = strlen(path);
  if (l2 >= sizeof(pathbuf))
    return 0;
  strcpy(pathbuf, path);
  if (pathbuf[0] != '/') 
    return 0;
  p = pathbuf + rootl;
  for (;;)
    {
      p = strchr(p, '/');
      if (!p)
        return 1;
      *p = 0;
      if (lstat(*pathbuf ? pathbuf : "/", &stb))
	return 0;
      if (S_ISLNK(stb.st_mode))
	{
	  if (++lcnt >= 256)
	    return 0;
	  l = readlink(pathbuf, linkbuf, sizeof(linkbuf));
	  if (l <= 0 || l >= sizeof(linkbuf))
	    return 0;
	  while(l && linkbuf[l - 1] == '/')
	    l--;
	  if (l + 1 >= sizeof(linkbuf))
	    return 0;
	  linkbuf[l++] = '/';
	  linkbuf[l] = 0;
	  *p++ = '/';
	  if (linkbuf[0] == '/')
	    {
	      if (rootl)
		{
		  p[-1] = 0;
		  fprintf(stderr, "can't handle symlink %s at the moment\n", pathbuf);
		  return 0;
		}
	      l2 -= (p - pathbuf);
	      memmove(pathbuf + rootl, p, l2 + 1);
	      l2 += rootl;
	      p = pathbuf + rootl;
	    }
	  else
	    {
	      if (p - 1 == pathbuf)
		return 0;		/* huh, "/" is a symlink */
	      for (p2 = p - 2; p2 >= pathbuf; p2--)
		if (*p2 == '/')
		  break;
	      if (p2 < pathbuf + rootl)	/* cannot happen */
		return 0;
	      p2++;			/* am now after '/' */
              memmove(p2, p, pathbuf + l2 - p + 1);
	      l2 -= (p - p2);
	      p = p2;
	    }
	  if (l + l2 >= sizeof(pathbuf))
	    return 0;
	  memmove(p + l, p, pathbuf + l2 - p + 1);
	  memmove(p, linkbuf, l);
	  l2 += l;
	  if (pathbuf[0] != '/')	/* cannot happen */
	    return 0;
	  if (p == pathbuf)
	    p++;
	  continue;
	}
      if (!S_ISDIR(stb.st_mode))
	return 0;

      /* write is always forbidden for other */
      if ((stb.st_mode & 02) != 0)
	return 0;

      /* owner must be ok as she may change the mode */
      /* for euid != 0 it is also ok if the owner is euid */
      if (stb.st_uid && stb.st_uid != uid && stb.st_uid != euid)
	return 0;

      /* group gid may do fancy things */
      /* for euid != 0 we don't check this */
      if ((stb.st_mode & 020) != 0 && !euid)
	if (!gid || stb.st_gid != gid)
	  return 0;

      *p++ = '/';
    }
}

int
main(int argc, char **argv)
{
  char *opt, *p;
  int set = 0;
  int told = 0;
  int use_checklist = 0;
  FILE *fp;
  char line[512];
  char *part[4];
  int i, pcnt, lcnt;
  int inpart;
  mode_t mode;
  struct perm *e;
  struct stat stb, stb2;
  struct passwd *pwd = 0;
  struct group *grp = 0;
  uid_t uid;
  gid_t gid;
  int fd, r;
  int errors = 0;

  while (argc > 1)
    {
      opt = argv[1];
      if (!strcmp(opt, "--"))
	break;
      if (*opt == '-' && opt[1] == '-')
	opt++;
      if (!strcmp(opt, "-s") || !strcmp(opt, "-set"))
	{
	  set = 1;
	  argc--;
	  argv++;
	  continue;
	}
      if (!strcmp(opt, "-n") || !strcmp(opt, "-noheader"))
	{
	  told = 1;
	  argc--;
	  argv++;
	  continue;
	}
      if (!strcmp(opt, "-e") || !strcmp(opt, "-examine"))
	{
	  argc--;
	  argv++;
	  if (argc == 1)
	    {
	      fprintf(stderr, "examine: argument required\n");
	      exit(1);
	    }
	  add_checklist(argv[1]);
	  use_checklist = 1;
	  argc--;
	  argv++;
	  continue;
	}
      if (!strcmp(opt, "-f") || !strcmp(opt, "-files"))
	{
	  argc--;
	  argv++;
	  if (argc == 1)
	    {
	      fprintf(stderr, "files: argument required\n");
	      exit(1);
	    }
	  if ((fp = fopen(argv[1], "r")) == 0)
	    {
	      fprintf(stderr, "files: %s: %s\n", argv[1], strerror(errno));
	      exit(1);
	    }
	  while (readline(fp, line, sizeof(line)))
	    {
	      if (!*line)
		continue;
	      add_checklist(line);
	    }
	  fclose(fp);
	  use_checklist = 1;
	  argc--;
	  argv++;
	  continue;
	}
      if (!strcmp(opt, "-r") || !strcmp(opt, "-root"))
	{
	  argc--;
	  argv++;
	  if (argc == 1)
	    {
	      fprintf(stderr, "root: argument required\n");
	      exit(1);
	    }
	  root = argv[1];
	  rootl = strlen(root);
	  if (*root != '/')
	    {
	      fprintf(stderr, "root: must begin with '/'\n");
	      exit(1);
	    }
	  argc--;
	  argv++;
	  continue;
	}
      if (*opt == '-')
	usage(!strcmp(opt, "-h") || !strcmp(opt, "-help") ? 0 : 1);
      break;
    }
  if (argc <= 1)
    usage(1);
  for (i = 1; i < argc; i++)
    {
      if ((fp = fopen(argv[i], "r")) == 0)
	{
	  perror(argv[i]);
	  exit(1);
	}
      lcnt = 0;
      while (readline(fp, line, sizeof(line)))
	{
	  lcnt++;
	  if (*line == 0 || *line == '#' || *line == '$')
	    continue;
	  inpart = 0;
	  pcnt = 0;
	  for (p = line; *p; p++)
	    {
	      if (*p == ' ' || *p == '\t')
		{
		  *p = 0;
		  if (inpart)
		    {
		      pcnt++;
		      inpart = 0;
		    }
		  continue;
		}
	      if (!inpart)
		{
		  inpart = 1;
		  if (pcnt == 3)
		    break;
		  part[pcnt] = p;
		}
	    }
	  if (inpart)
	    pcnt++;
	  if (pcnt != 3)
	    {
	      fprintf(stderr, "bad permissions line %s:%d\n", argv[i], lcnt);
	      continue;
	    }
	  part[3] = part[2];
	  part[2] = strchr(part[1], ':');
	  if (!part[2])
	    part[2] = strchr(part[1], '.');
	  if (!part[2])
	    {
	      fprintf(stderr, "bad permissions line %s:%d\n", argv[i], lcnt);
	      continue;
	    }
	  *part[2]++ = 0;
          mode = strtoul(part[3], part + 3, 8);
	  if (mode > 07777 || part[3][0])
	    {
	      fprintf(stderr, "bad permissions line %s:%d\n", argv[i], lcnt);
	      continue;
	    }
	  add_permlist(part[0], part[1], part[2], mode);
	}
      fclose(fp);
    }

  euid = geteuid();
  for (e = permlist; e; e = e->next)
    {
      if (use_checklist && !in_checklist(e->file))
	continue;
      if (lstat(e->file, &stb))
	continue;
      if (S_ISLNK(stb.st_mode))
	continue;
      if ((!pwd || strcmp(pwd->pw_name, e->owner)) && (pwd = getpwnam(e->owner)) == 0)
	{
	  fprintf(stderr, "%s: unknown user %s\n", e->file, e->owner);
	  continue;
	}
      if ((!grp || strcmp(grp->gr_name, e->group)) && (grp = getgrnam(e->group)) == 0)
	{
	  fprintf(stderr, "%s: unknown group %s\n", e->file, e->group);
	  continue;
	}
      uid = pwd->pw_uid;
      gid = grp->gr_gid;
      if ((stb.st_mode & 07777) == e->mode && stb.st_uid == uid && stb.st_gid == gid)
	continue;

      if (!told)
	{
	  told = 1;
	  printf("Checking permissions and ownerships - using the permissions files\n");
	  for (i = 1; i < argc; i++)
	    printf("\t%s\n", argv[i]);
	}

      if (!set)
        printf("%s should be %s:%s %04o.", e->file, e->owner, e->group, e->mode);
      else
        printf("setting %s to %s:%s %04o.", e->file, e->owner, e->group, e->mode);
      printf(" (wrong");
      if (stb.st_uid != uid || stb.st_gid != gid)
	{
	  pwd = getpwuid(stb.st_uid);
	  grp = getgrgid(stb.st_gid);
	  if (pwd)
	    printf(" owner/group %s", pwd->pw_name);
	  else
	    printf(" owner/group %d", stb.st_uid);
	  if (grp)
	    printf(":%s", grp->gr_name);
	  else
	    printf(":%d", stb.st_gid);
	  pwd = 0;
	  grp = 0;
	}
      if ((stb.st_mode & 07777) != e->mode)
	printf(" permissions %04o", (int)(stb.st_mode & 07777));
      putchar(')');
      putchar('\n');

      if (!set)
	continue;

      fd = -1;
      if (S_ISDIR(stb.st_mode))
	{
	  fd = open(e->file, O_RDONLY|O_DIRECTORY|O_NONBLOCK|O_NOFOLLOW);
	  if (fd == -1)
	    {
	      perror(e->file);
	      errors++;
	      continue;
	    }
	}
      else if (S_ISREG(stb.st_mode))
	{
	  fd = open(e->file, O_RDONLY|O_NONBLOCK|O_NOFOLLOW);
	  if (fd == -1)
	    {
	      perror(e->file);
	      errors++;
	      continue;
	    }
	  if (fstat(fd, &stb2))
	    continue;
	  if (stb.st_mode != stb2.st_mode || stb.st_nlink != stb2.st_nlink || stb.st_dev != stb2.st_dev || stb.st_ino != stb2.st_ino)
	    {
	      fprintf(stderr, "%s: too fluctuating\n", e->file);
	      errors++;
	      continue;
	    }
	  if (stb.st_nlink > 1 && !safepath(e->file, 0, 0))
	    {
	      fprintf(stderr, "%s: on an insecure path\n", e->file);
	      errors++;
	      continue;
	    }
	  else if (e->mode & 06000)
	    {
	      /* extra checks for s-bits */
	      if (!safepath(e->file, (e->mode & 02000) == 0 ? uid : 0, (e->mode & 04000) == 0 ? gid : 0))
		{
		  fprintf(stderr, "%s: will not give away s-bits on an insecure path\n", e->file);
		  errors++;
		  continue;
		}
	    }
	}
      else if (strncmp(e->file, "/dev/", 4) != 0)
	{
	  fprintf(stderr, "%s: don't know what to do with that type of file\n", e->file);
	  errors++;
	  continue;
	}
      if (euid == 0 && (stb.st_uid != uid || stb.st_gid != gid))
	{
	  if (fd >= 0)
	    r = fchown(fd, uid, gid);
	  else
	    r = chown(e->file, uid, gid);
	  if (r)
	    {
	      fprintf(stderr, "%s: chown: %s\n", e->file, strerror(errno));
	      errors++;
	    }
	  if (fd >= 0)
	    r = fstat(fd, &stb);
	  else
	    r = lstat(e->file, &stb);
	  if (r)
	    {
	      fprintf(stderr, "%s: too fluctuating\n", e->file);
	      errors++;
	      continue;
	    }
	}
      if ((stb.st_mode & 07777) != e->mode)
	{
	  if (fd >= 0)
	    r = fchmod(fd, e->mode);
	  else
	    r = chmod(e->file, e->mode);
	  if (r)
	    {
	      fprintf(stderr, "%s: chmod: %s\n", e->file, strerror(errno));
	      errors++;
	    }
	}
      if (fd >= 0)
	close(fd);
    }
  if (errors)
    {
      fprintf(stderr, "ERROR: not all operations were successful.\n");
      exit(1);
    }
  exit(0);
}