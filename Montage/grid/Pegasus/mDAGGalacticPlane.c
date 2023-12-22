/*

Version  Developer        Date     Change
-------  ---------------  -------  -----------------------
1.0      Mats Rynge       9Jun10   Adopted the mDAG code for the galactic
                                   plane runs - mainly changes to the
                                   projection and coordinate system



*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <mtbl.h>
#include <coord.h>
#include <wcs.h>
#include <svc.h>
#include <hashtable.h>

#define MAXSTR   1024

char *svc_value();

int   readTemplate(char *template);
int   printError  (char *msg);
int   stradd      (char *header, char *card);

int   strcasecmp  (const char *s1, const char *s2);

char *url_encode  (char *s);
int   tcp_connect (char *hostname, int port);

int   lookup      (char *mosaicCenter, double *ra, double *dec);

static struct WorldCoor *wcs;

int    naxis1, naxis2;
int    sys;
double epoch;

int    debug = 1;
FILE  *fdebug;

char   fmt   [MAXSTR];
char   dfmt  [MAXSTR];


/******************************************************************************/
/*                                                                            */
/* mDAGGalacticPlane                                                          */
/*                                                                            */
/* Creates an XML-based Directed Acyclic Graph (DAG) that specifies  "jobs"   */
/* or Montage modules to be run to produce a mosaic,  "files" to be           */
/* consumed or generated during processing (input, intermediate, and output   */
/* files), and "dependencies" between jobs and files that imply an order      */
/* in which jobs should be run.  In this "abstract graph", logical names are  */
/* assigned to jobs and files.                                                */
/******************************************************************************/

int main(int argc, char **argv)
{
   int     i, j, k, ncol, nimages;
   int     haveHdr, itmp, istat, mAddCntr;
   int     nparents, parent, parent_prev;
   int     naxis1s, naxis2s;
  
   char    line          [MAXSTR];
   char    mproj         [MAXSTR];
   char    survey        [MAXSTR];
   char    band          [MAXSTR];
   char    hdrFile       [MAXSTR];
   char    mosaicCentLon [MAXSTR];
   char    mosaicCentLat [MAXSTR];
   char    mosaicWidth   [MAXSTR];
   char    mosaicHeight  [MAXSTR];
   char    mosaicCdelt   [MAXSTR];
   char    workdir       [MAXSTR];
   char    urlbase       [MAXSTR];
   char    workurlbase   [MAXSTR];
   char    timestr       [MAXSTR];
   char    cmd           [MAXSTR];
   char    status        [MAXSTR];
   char    msg           [MAXSTR];
   char    fname         [MAXSTR];
   char    fitname       [MAXSTR];
   char    plusname      [MAXSTR];
   char    minusname     [MAXSTR];
   char    jobid         [MAXSTR];
   char    fileList      [MAXSTR];
   char    parentList    [MAXSTR];
   char    sortedParent  [MAXSTR];

   char   *fileid;
   char   *parentid;

   clock_t timeval;

   double  width;
   double  height;
   double  cdelt;
   double  crval1, crval2;
   double  crpix1, crpix2;

   int     ifname, iurl, icntr1, icntr2, iplusname, iminusname, iscale;
   int     cntr, cntr1, cntr2, id, level;

   int     shrinkFactor, shrinkFactorX, shrinkFactorY;

   double  x, y, z;
   double  xc, yc, zc;
   double  xpos, ypos, dtr;
   double  dist, maxRadius;
   double  lonc, latc;

   FILE   *fp;
   FILE   *fdag;
   FILE   *ffit;
   FILE   *fcache;
   FILE   *furl;
   FILE   *ffile;
   FILE   *fparent;

   char   dv_version  [MAXSTR];
   
   int    maxtbl = 1000;

   char   key[MAXSTR];
   char   val[MAXSTR];

   struct stat type;

   HT_table_t *depends;

   char *path = getenv("MONTAGE_HOME");


   /* Various time value variables */

   char       buffer[256];

   int        yr, mo, day, hr, min, sec, pid;

   time_t     curtime;
   struct tm *loctime;

   char       idstr[256];


   dtr = atan(1.0)/45.;

   strcpy(dv_version, "1.0");

   HT_set_debug(0); 


   /* Generate a unique ID based on data/time/pid */

   curtime = time (NULL);
   loctime = localtime (&curtime);

   strftime(buffer, 256, "%Y", loctime);
   yr = atoi(buffer);

   strftime(buffer, 256, "%m", loctime);
   mo = atoi(buffer);

   strftime(buffer, 256, "%d", loctime);
   day = atoi(buffer);

   strftime(buffer, 256, "%H", loctime);
   hr = atoi(buffer);

   strftime(buffer, 256, "%M", loctime);
   min = atoi(buffer);

   strftime(buffer, 256, "%S", loctime);
   sec = atoi(buffer);

   pid = (int)getpid();

   sprintf(idstr, "%04d%02d%02d_%02d%02d%02d_%d",
      yr, mo, day, hr, min, sec, pid);



   /* Set up the dependency hash table */

   depends = HT_create_table(maxtbl);

   timeval = time(0);

   strcpy(timestr, ctime((const time_t *)(&timeval)));

   for(i=0; i<strlen(timestr); ++i)
      if(timestr[i] == '\n')
	 timestr[i]  = '\0';

   if(debug)
   {
      fdebug = fopen("debug.txt", "w+");

      if(fdebug == (FILE *)NULL)
      {
	 printf("[struct stat=\"ERROR\", msg=\"Error opening debug file\"]\n");
	 exit(0);
      }

      fprintf(fdebug, "DEBUGGING mDAG:\n\n");
      fflush(fdebug);

      if(debug > 1)
	 svc_debug(fdebug);
   }



   /* Get the location/size information */

   haveHdr = 0;

   if(argc < 11)
   {
      printf("[struct stat=\"ERROR\", msg=\"Usage: %s survey band centerlon centerlat width height cdelt workdir workurlbase urlbase | %s -h survey band hdrfile workdir workurlbase urlbase (object/location string must be a single argument)\"]\n", argv[0], argv[0]);
      exit(0);
   }


   strcpy(survey,        argv[1]);
   strcpy(band,          argv[2]);
   strcpy(mosaicCentLon, argv[3]);
   strcpy(mosaicCentLat, argv[4]);
   strcpy(mosaicWidth,   argv[5]);
   strcpy(mosaicHeight,  argv[6]);
   strcpy(mosaicCdelt,   argv[7]);
   strcpy(workdir,       argv[8]);
   strcpy(workurlbase,   argv[9]);
   strcpy(urlbase,       argv[10]);

   crval1 = atof(mosaicCentLon);
   crval2 = atof(mosaicCentLat);
   width  = fabs(atof(mosaicWidth));
   height = fabs(atof(mosaicHeight));
   cdelt  = fabs(atof(mosaicCdelt));

   naxis1 = (int)(width  / cdelt) + 0.5;
   naxis2 = (int)(height / cdelt) + 0.5;

   crpix1 = (naxis1+1.)/2.;
   crpix2 = (naxis2+1.)/2.;

   istat = stat(workdir, &type);
   if(istat < 0)
      printError("work directory doesn't exist");

   strcpy(mproj, "mProject");


   /********************************************************/
   /* we have the size/center, we need to build the header */
   /********************************************************/

   /*******************************/
   /* Create header template file */
   /*******************************/

   sprintf(hdrFile, "%s/region.hdr", workdir);

   fp = fopen(hdrFile, "w+");

   if(fp == (FILE *)NULL)
   {
      printf("[struct stat=\"ERROR\", msg=\"Error opening header template file\"]\n");
	   exit(0);
   }

   fprintf(fp, "SIMPLE  = T\n"                  );
   fprintf(fp, "BITPIX  = -64\n"                );
   fprintf(fp, "NAXIS   = 2\n"                  );
   fprintf(fp, "NAXIS1  = %d\n",      naxis1    );
   fprintf(fp, "NAXIS2  = %d\n",      naxis2    );
   fprintf(fp, "CTYPE1  = '%s'\n",   "GLON-CAR" );
   fprintf(fp, "CTYPE2  = '%s'\n",   "GLAT-CAR" );
   fprintf(fp, "CRVAL1  = %11.6f\n",  crval1    );
   fprintf(fp, "CRVAL2  = %11.6f\n",  crval2    );
   fprintf(fp, "CRPIX1  = %11.6f\n",  crpix1    );
   fprintf(fp, "CRPIX2  = %11.6f\n",  crpix2    );
   fprintf(fp, "CDELT1  = %.9f\n",   -cdelt     );
   fprintf(fp, "CDELT2  = %.9f\n",    cdelt     );
   fprintf(fp, "CROTA2  = %11.6f\n",  0.0       );
   fprintf(fp, "EQUINOX = %d\n",      2000      );


   if(strcasecmp(survey, "2MASS") == 0)
   {
      if(strcasecmp(band, "J") == 0)
         fprintf(fp, "MAGZP   = %11.6f\n", 20.9044);
      else if(strcasecmp(band, "H") == 0)
         fprintf(fp, "MAGZP   = %11.6f\n", 20.4871);
      else if(strcasecmp(band, "K") == 0)
         fprintf(fp, "MAGZP   = %11.6f\n", 19.9757);
   }

   fprintf(fp, "END\n"                          );

   fclose(fp);


   /***********************************/
   /* Create big header template file */
   /***********************************/

   sprintf(hdrFile, "%s/big_region.hdr", workdir);

   fp = fopen(hdrFile, "w+");

   if(fp == (FILE *)NULL)
   {
      printf("[struct stat=\"ERROR\", msg=\"Error opening big header template file\"]\n");
      exit(0);
   }
   
   fprintf(fp, "SIMPLE  = T\n"                      );
   fprintf(fp, "BITPIX  = -64\n"                    );
   fprintf(fp, "NAXIS   = 2\n"                      );
   fprintf(fp, "NAXIS1  = %d\n",      naxis1+3000   );
   fprintf(fp, "NAXIS2  = %d\n",      naxis2+3000   );
   fprintf(fp, "CTYPE1  = '%s'\n",   "GLON-CAR"     );
   fprintf(fp, "CTYPE2  = '%s'\n",   "GLAT-CAR"     );
   fprintf(fp, "CRVAL1  = %11.6f\n",  crval1        );
   fprintf(fp, "CRVAL2  = %11.6f\n",  crval2        );
   fprintf(fp, "CRPIX1  = %11.6f\n",  crpix1+1500   );
   fprintf(fp, "CRPIX2  = %11.6f\n",  crpix2+1500   );
   fprintf(fp, "CDELT1  = %.9f\n",   -cdelt         );
   fprintf(fp, "CDELT2  = %.9f\n",    cdelt         );
   fprintf(fp, "CROTA2  = %11.6f\n",  0.0           );
   fprintf(fp, "EQUINOX = %d\n",      2000          );

   if(strcasecmp(survey, "2MASS") == 0)
   {
      if(strcasecmp(band, "J") == 0)
         fprintf(fp, "MAGZP   = %11.6f\n", 20.9044);

      else if(strcasecmp(band, "H") == 0)
         fprintf(fp, "MAGZP   = %11.6f\n", 20.4871);

      else if(strcasecmp(band, "K") == 0)
         fprintf(fp, "MAGZP   = %11.6f\n", 19.9757);
   }

   fprintf(fp, "END\n"                          );
   fclose(fp);


   /************************************************/
   /* Get list of raw input images for this region */
   /************************************************/

   /* this is how mExec is implemented */
   double scaleWidth = width * 1.42;
   double scaleHeight = height * 1.42;

   if(path)
      sprintf(cmd, "%s/bin/mArchiveList %s %s \"%s %s gal\" %.2f %.2f %s/images.tbl",
            path, survey, band, mosaicCentLon, mosaicCentLat, scaleWidth,
            scaleHeight, workdir);
   else 
      sprintf(cmd, "mArchiveList %s %s \"%s %s gal\" %.2f %.2f %s/images.tbl",
            survey, band, mosaicCentLon, mosaicCentLat, scaleWidth,
            scaleHeight, workdir);

   if(debug)
   {
      fprintf(fdebug, "[%s]\n", cmd);
      fflush(fdebug);
   }

   svc_run(cmd);

   strcpy( status, svc_value( "stat" ));

   if (strcmp( status, "ERROR") == 0)
   {
      strcpy( msg, svc_value( "msg" ));

      printError(msg);
   }

   if (strcmp(status, "ABORT") == 0)
   {
      strcpy( msg, svc_value( "msg" ));

      printError(msg);
   }

   nimages = atof(svc_value("count"));

   if (nimages == 0)
   {
      sprintf( msg, "%s has no data covering this area", survey);

      printError(msg);
   }



   /*****************************************/
   /* Get the image lists the DAG will need */
   /*****************************************/

   if(path)
      sprintf(cmd, "%s/bin/mDAGTbls %s/images.tbl %s/big_region.hdr %s/rimages.tbl %s/pimages.tbl %s/cimages.tbl",
         path, workdir, workdir, workdir, workdir, workdir);
      else 
         sprintf(cmd, "mDAGTbls %s/images.tbl %s/big_region.hdr %s/rimages.tbl %s/pimages.tbl %s/cimages.tbl",
            workdir, workdir, workdir, workdir, workdir);
 

   if(debug)
   {
      fprintf(fdebug, "[%s]\n", cmd);
      fflush(fdebug);
   }

   svc_run(cmd);

   strcpy( status, svc_value( "stat" ));

   if (strcmp( status, "ERROR") == 0)
   {
      strcpy( msg, svc_value( "msg" ));

      printError(msg);
   }



   /******************************************/
   /* Get the overlap list the DAG will need */
   /******************************************/

   if(path)
      sprintf(cmd, "%s/bin/mOverlaps %s/rimages.tbl %s/diffs.tbl",
         path, workdir, workdir);
      else 
         sprintf(cmd, " mOverlaps %s/rimages.tbl %s/diffs.tbl",
            workdir, workdir);

   if(debug)
   {
      fprintf(fdebug, "[%s]\n", cmd);
      fflush(fdebug);
   }

   svc_run(cmd);

   strcpy( status, svc_value( "stat" ));

   if (strcmp( status, "ERROR") == 0)
   {
      strcpy( msg, svc_value( "msg" ));

      printError(msg);
   }


   

   /****************************************************/
   /*  Determine shrink factor for presentation image. */
   /****************************************************/

   shrinkFactorX = (int)(naxis1 / 1024. + 1.);
   shrinkFactorY = (int)(naxis2 / 1024. + 1.);

   shrinkFactor = shrinkFactorX;
   if(shrinkFactor < shrinkFactorY)
      shrinkFactor = shrinkFactorY;

   if(debug)
   {
      fprintf(fdebug, "\n");
      fprintf(fdebug, "shrinkFactorX = %d\n", shrinkFactorX);
      fprintf(fdebug, "shrinkFactorY = %d\n", shrinkFactorY);
      fprintf(fdebug, "shrinkFactor  = %d\n", shrinkFactor);
      fprintf(fdebug, "\n");
      fflush(fdebug);
   }


   /***********************************************/
   /* Now we can generate the DAG XML file itself */
   /* First the XML header                        */
   /***********************************************/

   if(debug)
   {
      fprintf(fdebug, "Generating DAG file header ...\n");
      fflush(fdebug);
   }

   sprintf(cmd, "%s/dag.xml", workdir);
   fdag = fopen(cmd, "w+");

   sprintf(cmd, "%s/cache.list", workdir);
   fcache = fopen(cmd, "w+");

   sprintf(cmd, "%s/url.list", workdir);
   furl = fopen(cmd, "w+");

   fprintf(fdag, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
   fprintf(fdag, "<!-- Generated:    %s -->\n", timestr);
   fprintf(fdag, "<!-- Generated by: Montage DAG service      -->\n\n");
   fprintf(fdag, "<adag xmlns=\"http://pegasus.isi.edu/schema/DAX\"\n");
   fprintf(fdag, "      xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
   fprintf(fdag, "      xsi:schemaLocation=\"http://pegasus.isi.edu/schema/DAX http://pegasus.isi.edu/schema/dax-2.1.xsd\"\n");
   fprintf(fdag, "      version=\"2.1\" count=\"1\" index=\"0\" name=\"montage\">\n\n");

   fflush(fdag);

   fprintf(fdag, "  <!-- Survey:            %-30s -->\n", survey);
   fprintf(fdag, "  <!-- Band:              %-30s -->\n", band);
   fprintf(fdag, "  <!-- Center Longitude:  %-30s -->\n", mosaicCentLon);
   fprintf(fdag, "  <!-- Center Latitude:   %-30s -->\n", mosaicCentLat);
   
   if(haveHdr)
      fprintf(fdag, "  <!-- (WCS is in input header file)           -->\n");
   else
   {
      fprintf(fdag, "  <!-- Width:   %-30s -->\n", mosaicWidth);
      fprintf(fdag, "  <!-- Height:  %-30s -->\n", mosaicHeight);
   }

   fprintf(fdag, "\n\n");
   fflush(fdag);

   /******************************************/
   /* compose the url lfn cache list         */
   /******************************************/

   sprintf(cmd, "%s/images.tbl", workdir);

   ncol = topen(cmd);

   ifname = tcol("file");
   iurl = tcol("URL");

   if(ifname < 0)
      printError("'Raw' image list does not have column 'file'");

   if(iurl < 0)
      printError("'Raw' image list does not have column 'url'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      fprintf(furl, "%s %s pool=\"ipac_cluster\"\n", tval(ifname), tval(iurl));
   }

   tclose();
   fclose(furl);


   /******************************************/
   /* DAG: "filename" lines for input images */
   /******************************************/

   if(debug)
   {
      fprintf(fdebug, "Generating DAG file filename list ...\n");
      fflush(fdebug);
   }

   fprintf(fdag, "  <!-- Part 1:  Files Used -->\n\n");

   sprintf(cmd, "%s/rimages.tbl", workdir);

   ncol = topen(cmd);

   ifname = tcol("file");

   if(ifname < 0)
      printError("'Raw' image list does not have column 'file'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      fprintf(fdag, "  <filename file=\"%s\" link=\"input\"/>\n", tval(ifname));
   }

   fprintf(fdag, "\n");
   fflush(fdag);
   tclose();



   /**********************************************/
   /* DAG: "filename" lines for projected images */
   /* including area files                       */
   /**********************************************/

   sprintf(cmd, "%s/pimages.tbl", workdir);

   ncol = topen(cmd);

   ifname = tcol("file");

   if(ifname < 0)
      printError("'Projected' image list does not have column 'file'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      fprintf(fdag, "  <filename file=\"%s\" link=\"inout\"/>\n", tval(ifname));

      strcpy(fname, tval(ifname));

      fname[strlen(fname)-5] = '\0';

      strcat(fname, "_area.fits");

      fprintf(fdag, "  <filename file=\"%s\" link=\"inout\"/>\n", fname);
   }

   fprintf(fdag, "\n");
   fflush(fdag);
   tclose();



   /***********************************************/
   /* DAG: "filename" lines for difference images */
   /* and fit output files (plus one more file    */
   /* with a list of these fits)                  */
   /***********************************************/

   sprintf(cmd, "%s/diffs.tbl", workdir);

   ncol   = topen(cmd);
   ifname = tcol("diff");
   icntr1 = tcol("cntr1");
   icntr2 = tcol("cntr2");

   if(ifname < 0)
      printError("'Diff' image list does not have column 'diff'");

   sprintf(cmd, "%s/statfile.tbl", workdir);

   ffit = fopen(cmd, "w+");

   sprintf(fmt, "|%%7s|%%7s|%%22s|\n");
   sprintf(dfmt, " %%7d %%7d %%22s \n");

   fprintf(ffit, fmt, "cntr1", "cntr2", "stat");
   fprintf(ffit, fmt, "int", "int", "char");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      cntr1 = atoi(tval(icntr1));
      cntr2 = atoi(tval(icntr2));

      fprintf(fdag, "  <filename file=\"%s\" link=\"output\"/>\n", tval(ifname));

      strcpy(fname, tval(ifname));

      fname[strlen(fname)-5] = '\0';

      sprintf(fitname, "fit%s.txt", fname+4);

      fprintf(fdag, "  <filename file=\"%s\" link=\"inout\"/>\n\n", fitname);

      fprintf(ffit, dfmt, cntr1, cntr2, fitname);
   }

   fflush(fdag);
   tclose();

   fclose(ffit);

   fprintf(fdag, "  <filename file=\"statfile_%s.tbl\" link=\"input\"/>\n", idstr);
   fprintf(fdag, "  <filename file=\"fits.tbl\" link=\"inout\"/>\n");
   fprintf(fdag, "\n");
   fflush(fdag);

   fprintf(fcache, "statfile_%s.tbl %s/%s/statfile.tbl pool=\"ipac_cluster\"\n", idstr, workurlbase, workdir);


   /***********************************************/
   /* DAG: "filename" lines for corrections table */
   /* and the corrected image files (including    */
   /* area files)                                 */
   /***********************************************/

   fprintf(fdag, "  <filename file=\"corrections.tbl\" link=\"inout\"/>\n");
   fprintf(fdag, "\n");
   fflush(fdag);

   sprintf(cmd, "%s/cimages.tbl", workdir);

   ncol = topen(cmd);
   ifname = tcol("file");

   if(ifname < 0)
      printError("'Corrected' image list does not have column 'file'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      fprintf(fdag, "  <filename file=\"%s\" link=\"inout\"/>\n", tval(ifname));

      strcpy(fname, tval(ifname));

      fname[strlen(fname)-5] = '\0';

      strcat(fname, "_area.fits");

      fprintf(fdag, "  <filename file=\"%s\" link=\"inout\"/>\n", fname);
   }

   fprintf(fdag, "\n");
   fflush(fdag);
   tclose();
   

   /***********************************************/
   /* DAG: "filename" lines for template files,   */
   /* subset image files, output files, and       */
   /* shrunken output files.                      */
   /***********************************************/

   fprintf(fdag, "  <filename file=\"newcimages.tbl\" link=\"inout\"/>\n");
   fprintf(fdag, "\n");
   fflush(fdag);

   fprintf(fdag, "  <filename file=\"big_region_%s.hdr\" link=\"input\"/>\n", idstr);
   fprintf(fdag, "  <filename file=\"region_%s.hdr\" link=\"input\"/>\n", idstr);
   fprintf(fdag, "  <filename file=\"pimages_%s.tbl\" link=\"input\"/>\n", idstr);
   fprintf(fdag, "  <filename file=\"cimages_%s.tbl\" link=\"input\"/>\n", idstr);
   fprintf(fdag, "  <filename file=\"dag_%s.xml\" link=\"inout\"/>\n", idstr);
   fprintf(fdag, "  <filename file=\"images_%s.tbl\" link=\"inout\"/>\n", idstr);
   fprintf(fdag, "\n");

   fprintf(fdag, "  <filename file=\"shrunken_%s.hdr\" link=\"inout\"/>\n", idstr);

   fprintf(fdag, "  <filename file=\"shrunken_%s.fits\" link=\"inout\"/>\n", idstr);
   fprintf(fdag, "  <filename file=\"shrunken_%s.jpg\" link=\"output\"/>\n", idstr);
   fprintf(fdag, "\n\n");
   fflush(fdag);

   fprintf(fcache, "big_region_%s.hdr %s/%s/big_region.hdr pool=\"ipac_cluster\"\n", idstr, workurlbase, workdir);
   fprintf(fcache, "region_%s.hdr %s/%s/region.hdr pool=\"ipac_cluster\"\n", idstr, workurlbase, workdir);

   fprintf(fcache, "pimages_%s.tbl %s/%s/pimages.tbl pool=\"ipac_cluster\"\n", idstr, workurlbase, workdir);
   fprintf(fcache, "cimages_%s.tbl %s/%s/cimages.tbl pool=\"ipac_cluster\"\n", idstr, workurlbase, workdir);
   fflush(fcache);

   /* to be saved in user's storage space */
   fprintf(fcache, "dag_%s.xml %s/%s/dag.xml pool=\"ipac_cluster\"\n", idstr, workurlbase, workdir);
   fprintf(fcache, "images_%s.tbl %s/%s/images.tbl pool=\"ipac_cluster\"\n", idstr, workurlbase, workdir);

   /**************************************************/
   /* DAG: "job" lines for mProject/mProjectPP jobs. */
   /**************************************************/

   if(debug)
   {
      fprintf(fdebug, "Generating DAG file job list ...\n");
      fflush(fdebug);
   }

   id    = 0;
   level = 9;

   fprintf(fdag, "  <!-- Part 2:  Definition of Jobs -->\n\n");

   cntr = 0;

   sprintf(cmd, "%s/rimages.tbl", workdir);

   ncol   = topen(cmd);

   ifname = tcol("file");
   iscale = tcol("scale");

   if(ifname < 0)
      printError("'Raw' image list does not have column 'file'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      ++id;
      ++cntr;

      strcpy(fname, tval(ifname));

      fname[strlen(fname) - 5] = '\0';

      fprintf(fdag,"  <job id=\"ID%06d\" name=\"%s\" version=\"3.0\" level=\"%d\" dv-name=\"mProject%d\" dv-version=\"1.0\">\n", id, mproj, level, cntr);

      if(iscale >= 0)
	 fprintf(fdag,"    <argument>\n      -X\n      -x %s\n      <filename file=\"%s.fits\"/>\n      <filename file=\"p%s.fits\"/>\n      <filename file=\"big_region_%s.hdr\"/>\n    </argument>\n\n", tval(iscale), fname, fname, idstr);
      else
	 fprintf(fdag,"    <argument>\n      -X\n      <filename file=\"%s.fits\"/>\n      <filename file=\"p%s.fits\"/>\n      <filename file=\"big_region_%s.hdr\"/>\n    </argument>\n\n", fname, fname, idstr);

      fprintf(fdag,"    <uses file=\"%s.fits\" link=\"input\" transfer=\"true\"/>\n", fname);

      sprintf(key, "ID%06d", id);
      sprintf(val, "%s.fits",  fname);
      HT_add_entry(depends, key, val);

      fprintf(fcache,"%s.fits %s/%s.fits pool=\"ipac_cluster\"\n",fname,urlbase,fname);

      fprintf(fdag,"    <uses file=\"p%s.fits\" link=\"output\" register=\"false\" transfer=\"false\"/>\n", fname);

      sprintf(key, "p%s.fits", fname);
      sprintf(val, "ID%06d", id);
      HT_add_entry(depends, key, val);

      fprintf(fdag,"    <uses file=\"p%s_area.fits\" link=\"output\" register=\"false\" transfer=\"false\"/>\n", fname);

      fprintf(fdag,"    <uses file=\"big_region_%s.hdr\" link=\"input\" transfer=\"true\"/>\n", idstr);

      sprintf(key, "ID%06d", id);
      sprintf(val, "big_region.hdr");
      HT_add_entry(depends, key, val);

      fprintf(fdag,"  </job>\n\n\n");
   }

   tclose();
   fflush(fcache);
   fclose(fcache);



   /***************************************/
   /* DAG: "job" lines for mDiffFit jobs. */
   /***************************************/

   cntr = 0;

   sprintf(cmd, "%s/diffs.tbl", workdir);

   ncol      = topen(cmd);

   icntr1     = tcol("cntr1");
   icntr2     = tcol("cntr2");
   iplusname  = tcol("plus");
   iminusname = tcol("minus");

   --level;

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      ++id;
      ++cntr;

      cntr1 = atoi(tval(icntr1));
      cntr2 = atoi(tval(icntr2));

      strcpy(plusname,  tval(iplusname));
      strcpy(minusname, tval(iminusname));

      plusname [strlen(plusname)  - 5] = '\0';
      minusname[strlen(minusname) - 5] = '\0';

      fname[strlen(fname) - 5] = '\0';

      fprintf(fdag, "  <job id=\"ID%06d\" name=\"mDiffFit\" version=\"3.0\" level=\"%d\" dv-name=\"mDiffFit%d\" dv-version=\"1.0\">\n", id, level, cntr);

      fprintf(fdag, "    <argument>\n      -s <filename file=\"fit.%06d.%06d.txt\"/>\n      <filename file=\"p%s.fits\"/>\n      <filename file=\"p%s.fits\"/>\n      <filename file=\"diff.%06d.%06d.fits\"/>\n      <filename file=\"big_region_%s.hdr\"/>\n    </argument>\n\n", 
         cntr1, cntr2, plusname, minusname, cntr1, cntr2, idstr);

      fprintf(fdag, "    <uses file=\"mDiff\" link=\"input\" transfer=\"true\" type=\"executable\"/>\n");
      fprintf(fdag, "    <uses file=\"mFitplane\" link=\"input\" transfer=\"true\" type=\"executable\"/>\n");

      fprintf(fdag, "    <uses file=\"fit.%06d.%06d.txt\" link=\"output\" register=\"false\" transfer=\"false\"/>\n",
         cntr1, cntr2);

      sprintf(key, "fit.%06d.%06d.txt",  cntr1, cntr2);
      sprintf(val, "ID%06d", id);
      HT_add_entry(depends, key, val);

      fprintf(fdag, "    <uses file=\"p%s.fits\" link=\"input\" transfer=\"true\"/>\n",
         plusname);

      sprintf(key, "ID%06d", id);
      sprintf(val, "p%s.fits",  plusname);
      HT_add_entry(depends, key, val);


      fprintf(fdag, "    <uses file=\"p%s_area.fits\" link=\"input\" transfer=\"true\"/>\n", 
         plusname);

      fprintf(fdag, "    <uses file=\"p%s.fits\" link=\"input\" transfer=\"true\"/>\n",
         minusname);

      sprintf(key, "ID%06d", id);
      sprintf(val, "p%s.fits",  minusname);
      HT_add_entry(depends, key, val);

      fprintf(fdag, "    <uses file=\"p%s_area.fits\" link=\"input\" transfer=\"true\"/>\n", 
         minusname);

      fprintf(fdag, "    <uses file=\"diff.%06d.%06d.fits\" link=\"output\" register=\"false\" transfer=\"false\" optional=\"true\"/>\n", cntr1, cntr2);

      sprintf(key, "diff.%06d.%06d.fits",  cntr1, cntr2);
      sprintf(val, "ID%06d", id);
      HT_add_entry(depends, key, val);


      fprintf(fdag, "    <uses file=\"big_region_%s.hdr\" link=\"input\" transfer=\"true\"/>\n", idstr);

      sprintf(key, "ID%06d", id);
      sprintf(val, "big_region.hdr");
      HT_add_entry(depends, key, val);

      fprintf(fdag, "  </job>\n\n\n");
   }

   tclose();



   /****************************************/
   /* DAG: "job" line for mConcatFit jobs. */
   /****************************************/

   ++id;

   --level;

   fprintf(fdag, "  <job id=\"ID%06d\" name=\"mConcatFit\" version=\"3.0\" level=\"%d\" dv-name=\"mConcatFit1\" dv-version=\"1.0\">\n", id, level);

   fprintf(fdag, "    <argument>\n      <filename file=\"statfile_%s.tbl\"/>\n      <filename file=\"fits.tbl\"/>\n      .\n    </argument>\n\n", idstr);

   fprintf(fdag, "    <uses file=\"statfile_%s.tbl\" link=\"input\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "ID%06d", id);
   sprintf(val, "statfile.tbl");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"fits.tbl\" link=\"output\" register=\"false\" transfer=\"false\"/>\n");

   sprintf(key, "fits.tbl");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   sprintf(cmd, "%s/diffs.tbl", workdir);

   ncol   = topen(cmd);
   icntr1 = tcol("cntr1");
   icntr2 = tcol("cntr2");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      cntr1 = atoi(tval(icntr1));
      cntr2 = atoi(tval(icntr2));

      fprintf(fdag, "    <uses file=\"fit.%06d.%06d.txt\" link=\"input\" transfer=\"true\"/>\n", 
         cntr1, cntr2);

      sprintf(key, "ID%06d", id);
      sprintf(val, "fit.%06d.%06d.txt", cntr1, cntr2);
      HT_add_entry(depends, key, val);
   }

   tclose();
      
   fprintf(fdag, "  </job>\n\n\n");



   /*************************************/
   /* DAG: "job" line for mBgModel job. */
   /*************************************/

   ++id;

   --level;

   fprintf(fdag, "  <job id=\"ID%06d\" name=\"mBgModel\" version=\"3.0\" level=\"%d\" dv-name=\"mBgModel1\" dv-version=\"1.0\">\n", id, level);

   fprintf(fdag, "    <argument>\n      -l\n      -i 100000\n      <filename file=\"pimages_%s.tbl\"/>\n      <filename file=\"fits.tbl\"/>\n      <filename file=\"corrections.tbl\"/>\n    </argument>\n\n", idstr);

   fprintf(fdag, "    <uses file=\"pimages_%s.tbl\" link=\"input\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "ID%06d", id);
   sprintf(val, "pimages.tbl");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"fits.tbl\" link=\"input\" transfer=\"true\"/>\n");

   sprintf(key, "ID%06d", id);
   sprintf(val, "fits.tbl");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"corrections.tbl\" link=\"output\" register=\"false\" transfer=\"false\"/>\n");

   sprintf(key, "corrections.tbl");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   fprintf(fdag, "  </job>\n\n\n");



   /*****************************************/
   /* DAG: "job" line for mBackground jobs. */
   /*****************************************/

   --level;

   cntr = 0;

   sprintf(cmd, "%s/rimages.tbl", workdir);

   ncol   = topen(cmd);
   ifname = tcol("file");

   if(ifname < 0)
      printError("'Raw' image list does not have column 'file'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      strcpy(fname, tval(ifname));

      fname[strlen(fname) - 5] = '\0';

      ++id;
      ++cntr;

      fprintf(fdag, "  <job id=\"ID%06d\" name=\"mBackground\" version=\"3.0\" level=\"%d\" dv-name=\"mBackground%d\" dv-version=\"1.0\">\n", id, level, cntr);

      fprintf(fdag, "    <argument>\n      -t\n      <filename file=\"p%s.fits\"/>\n      <filename file=\"c%s.fits\"/>\n      <filename file=\"pimages_%s.tbl\"/>\n      <filename file=\"corrections.tbl\"/>\n    </argument>\n\n", 
         fname, fname, idstr);

      fprintf(fdag, "    <uses file=\"p%s.fits\" link=\"input\" transfer=\"true\"/>\n",
         fname);

      sprintf(key, "ID%06d", id);
      sprintf(val, "p%s.fits", fname);
      HT_add_entry(depends, key, val);

      fprintf(fdag, "    <uses file=\"p%s_area.fits\" link=\"input\" transfer=\"true\"/>\n", 
         fname);

      fprintf(fdag, "    <uses file=\"pimages_%s.tbl\" link=\"input\" transfer=\"true\"/>\n", idstr);

      sprintf(key, "ID%06d", id);
      sprintf(val, "pimages.tbl");
      HT_add_entry(depends, key, val);

      fprintf(fdag, "    <uses file=\"corrections.tbl\" link=\"input\" transfer=\"true\"/>\n");

      sprintf(key, "ID%06d", id);
      sprintf(val, "corrections.tbl");
      HT_add_entry(depends, key, val);

      fprintf(fdag, "    <uses file=\"c%s.fits\" link=\"output\" register=\"false\" transfer=\"false\"/>\n", 
         fname);

      sprintf(key, "c%s.fits", fname);
      sprintf(val, "ID%06d", id);
      HT_add_entry(depends, key, val);

      fprintf(fdag, "    <uses file=\"c%s_area.fits\" link=\"output\" register=\"false\" transfer=\"false\"/>\n", 
         fname);

      fprintf(fdag, "  </job>\n\n\n");
   }

   fflush(fdag);



   /**********************************************/
   /* DAG: "job" lines for tile mImgtbl jobs.    */
   /* We have to regenerate the cimages table(s) */
   /* because the pixel offsets and sizes need   */
   /* to be exactly right and the original is    */
   /* only an approximation.                     */
   /**********************************************/

   --level;

   cntr = 0;

   ++id;
   ++cntr;

   fprintf(fdag, "  <job id=\"ID%06d\" name=\"mImgtbl\" version=\"3.0\" level=\"%d\" dv-name=\"mImgtbl%d\" dv-version=\"1.0\">\n", 
         id, level, cntr);

   fprintf(fdag, "    <argument>\n      .\n      -t <filename file=\"cimages_%s.tbl\"/>\n      <filename file=\"newcimages.tbl\"/>\n    </argument>\n\n", idstr);

   fprintf(fdag, "    <uses file=\"cimages_%s.tbl\" link=\"input\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "ID%06d", id);
   sprintf(val, "cimages.tbl");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"newcimages.tbl\" link=\"output\" register=\"false\" transfer=\"false\"/>\n");

   sprintf(key, "newcimages.tbl");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   sprintf(cmd, "%s/cimages.tbl", workdir);

   ncol = topen(cmd);
   ifname = tcol("file");

   if(ifname < 0)
      printError("'Corrected' image list does not have column 'file'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      strcpy(fname, tval(ifname));

      fname[strlen(fname) - 5] = '\0';

      fprintf(fdag, "    <uses file=\"%s.fits\" link=\"input\" transfer=\"true\"/>\n", 
            fname);

      sprintf(key, "ID%06d", id);
      sprintf(val, "%s.fits", fname);
      HT_add_entry(depends, key, val);
   }

   tclose();

   fprintf(fdag, "  </job>\n\n\n");
   fflush(fdag);


   /*******************************************/
   /* DAG: "job" lines for tile mAdd jobs.    */
   /*******************************************/

   --level;

   cntr = 0;

   ++id;
   ++cntr;

   fprintf(fdag, "  <job id=\"ID%06d\" name=\"mAdd\" version=\"3.0\" level=\"%d\" dv-name=\"mAdd%d\" dv-version=\"1.0\">\n", 
         id, level, cntr);

   fprintf(fdag, "    <argument>\n      -e\n      <filename file=\"newcimages.tbl\"/>\n      <filename file=\"region_%s.hdr\"/>\n      <filename file=\"mosaic_%s.fits\"/>\n    </argument>\n\n", idstr, idstr);

   fprintf(fdag, "    <uses file=\"newcimages.tbl\" link=\"input\" transfer=\"true\"/>\n");

   sprintf(key, "ID%06d", id);
   sprintf(val, "newcimages.tbl");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"region_%s.hdr\" link=\"input\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "ID%06d", id);
   sprintf(val, "region.hdr");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"mosaic_%s.fits\" link=\"output\" register=\"true\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "mosaic.fits");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"mosaic_%s_area.fits\" link=\"output\" register=\"true\" transfer=\"true\"/>\n", idstr);

   sprintf(cmd, "%s/cimages.tbl", workdir);

   ncol = topen(cmd);
   ifname = tcol("file");

   if(ifname < 0)
      printError("'Corrected' image list does not have column 'file'");

   while(1)
   {
      istat = tread();

      if(istat < 0)
         break;

      strcpy(fname, tval(ifname));

      fname[strlen(fname) - 5] = '\0';

      fprintf(fdag, "    <uses file=\"%s.fits\" link=\"input\" transfer=\"true\"/>\n", 
            fname);

      fprintf(fdag, "    <uses file=\"%s_area.fits\" link=\"input\" transfer=\"true\"/>\n", 
            fname);

      sprintf(key, "ID%06d", id);
      sprintf(val, "%s.fits",  fname);
      HT_add_entry(depends, key, val);

   }

   fprintf(fdag, "  </job>\n\n\n");
   fflush(fdag);

   mAddCntr = cntr;



   /*******************************************/
   /* DAG: "job" lines for tile mShrink jobs. */
   /*******************************************/

   --level;

   cntr = 0;

   ++id;
   ++cntr;

   fprintf(fdag, "  <job id=\"ID%06d\" name=\"mShrink\" version=\"3.0\" level=\"%d\" dv-name=\"mShrink%d\" dv-version=\"1.0\">\n", id, level, cntr);

   fprintf(fdag, "    <argument>\n      <filename file=\"mosaic_%s.fits\"/>\n      <filename file=\"shrunken_%s.fits\"/>\n      %d\n    </argument>\n\n", idstr, idstr, shrinkFactor);

   fprintf(fdag, "    <uses file=\"mosaic_%s.fits\" link=\"input\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "ID%06d", id);
   sprintf(val, "mosaic.fits");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"shrunken_%s.fits\" link=\"output\" register=\"true\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "shrunken.fits");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   fprintf(fdag, "  </job>\n\n\n");

   fflush(fdag);


   /****************************/
   /* DAG: Make the final JPEG */
   /****************************/

   ++id;

   --level;

   fprintf(fdag, "  <job id=\"ID%06d\" name=\"mJPEG\" version=\"3.0\" level=\"%d\" dv-name=\"mJPEG1\" dv-version=\"1.0\">\n", id, level);

   fprintf(fdag, "    <argument>\n      -ct 1\n      -gray <filename file=\"shrunken_%s.fits\"/>\n      min max gaussianlog\n      -out <filename file=\"shrunken_%s.jpg\"/>\n    </argument>\n\n", idstr, idstr);

   fprintf(fdag, "    <uses file=\"shrunken_%s.fits\" link=\"input\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "ID%06d", id);
   sprintf(val, "shrunken.fits");
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"shrunken_%s.jpg\" link=\"output\" register=\"true\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "shrunken.jpg");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"dag_%s.xml\" link=\"input\" transfer=\"true\"/>\n", idstr);
   fprintf(fdag, "    <uses file=\"dag_%s.xml\" link=\"output\" register=\"false\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "dag.xml");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   fprintf(fdag, "    <uses file=\"images_%s.tbl\" link=\"input\" transfer=\"true\"/>\n", idstr);
   fprintf(fdag, "    <uses file=\"images_%s.tbl\" link=\"output\" register=\"false\" transfer=\"true\"/>\n", idstr);

   sprintf(key, "images.tbl");
   sprintf(val, "ID%06d", id);
   HT_add_entry(depends, key, val);

   fprintf(fdag, "  </job>\n\n\n\n");

   fflush(fdag);



   /*******************************************/
   /* DAG: Flow control dependencies.         */
   /* First, mDiffs depend on mProjects       */
   /*******************************************/

   if(debug)
   {
      fprintf(fdebug, "Generating DAG file parent/child info ...\n");
      fflush(fdebug);
   }

   fprintf(fdag, "  <!-- Part 3:  Control-Flow Dependencies -->\n\n");

   sprintf(fileList,     "%s/files.lis",   workdir);
   sprintf(parentList,   "%s/parents.lis", workdir);
   sprintf(sortedParent, "%s/sortedParents.lis", workdir);

   for(i=1; i<=id; ++i)
   {
      sprintf(jobid, "ID%06d", i);

      fileid = HT_lookup_key(depends, jobid);

      ffile = fopen(fileList, "w+");

      if(ffile == (FILE *)NULL)
      {
	 printf("[struct stat=\"ERROR\", msg=\"Error opening working file list for write\"]\n");
	 exit(0);
      }

      while(fileid != (char *)NULL)
      {
	 fprintf(ffile, "%s\n", fileid);
	 fflush(ffile);

	 fileid = HT_next_entry(depends);
      }

      fclose(ffile);

      ffile = fopen(fileList, "r");

      if(ffile == (FILE *)NULL)
      {
	 printf("[struct stat=\"ERROR\", msg=\"Error opening working file list for read\"]\n");
	 exit(0);
      }

      fparent = fopen(parentList, "w+");

      if(fparent == (FILE *)NULL)
      {
	 printf("[struct stat=\"ERROR\", msg=\"Error opening parent list\"]\n");
	 exit(0);
      }

      nparents = 0;

      fprintf(fparent, "|   parent   |\n");

      while(1)
      {
	 if(fgets(line, MAXSTR, ffile) == (char *)NULL)
	    break;

	 if(line[strlen(line)-1] == '\n')
	    line[strlen(line)-1]  = '\0';

	 parentid = HT_lookup_key(depends, line);

	 while(parentid != (char *)NULL)
	 {
	    parent = atoi(parentid+2);

	    fprintf(fparent, " %12d\n", parent);
	    fflush(fparent);

	    ++nparents;

	    parentid = HT_next_entry(depends);
	 }
      }

      fclose(ffile);
      fclose(fparent);

      unlink(fileList);

      if(nparents > 0)
      {

      if(path)
	 sprintf(cmd, "%s/bin/mTblSort %s parent %s", path, parentList, sortedParent);
         else 
	    sprintf(cmd, "mTblSort %s parent %s", parentList, sortedParent);
	 
	 svc_run(cmd);

	 strcpy( status, svc_value( "stat" ));

	 if (strcmp( status, "ERROR") == 0)
	 {
	    strcpy( msg, svc_value( "msg" ));

	    printError(msg);
	 }

         unlink(parentList);

	 fparent = fopen(sortedParent, "r");

	 if(fparent == (FILE *)NULL)
	 {
	    printf("[struct stat=\"ERROR\", msg=\"Error opening sorted parent list\"]\n");
	    exit(0);
	 }

	 fgets(line, MAXSTR, fparent);

	 fprintf(fdag, "  <child ref=\"%s\">\n", jobid);

	 for(j=0; j<nparents; ++j)
	 {
            if(fgets(line, MAXSTR, fparent) == (char *)NULL)
	       break;
	    
	    parent_prev = parent;
	    parent = atoi(line);

	    if(j > 0 && parent == parent_prev)
	       continue;

	    fprintf(fdag, "    <parent ref=\"ID%06d\"/>\n", parent);
	 }

	 fprintf(fdag, "  </child>\n\n");
	 fflush(fdag);

         fclose(fparent);
      }
      unlink(sortedParent);
   }

   fprintf(fdag, "</adag>\n");
   fflush(fdag);
   fclose(fdag);

   if(debug)
   {
      fprintf(fdebug, "done.\n");
      fflush(fdebug);
   }

   printf("[struct stat=\"OK\", id=\"%s\"]\n", idstr);
   exit(0);
}






/**************************************************/
/*                                                */
/*  Read the output header template file.         */
/*  Specifically extract the image size info.     */
/*  Also, create a single-string version of the   */
/*  header data and use it to initialize the      */
/*  output WCS transform.                         */
/*                                                */
/**************************************************/

int readTemplate(char *template)
{
   int       j;
   FILE     *fp;
   char      line[MAXSTR];
   char      header[80000];

   fp = fopen(template, "r");

   if(fp == (FILE *)NULL)
   {
      printf("[struct stat=\"ERROR\", msg=\"Bad template: %s\"]\n",
         template);
      fflush(stdout);
      exit(1);
   }

   strcpy(header, "");

   if(debug)
   {
      fprintf(fdebug, "\nTemplate Header (%s):\n", 
         template);
      fflush(fdebug);
   }

   for(j=0; j<1000; ++j)
   {
      if(fgets(line, MAXSTR, fp) == (char *)NULL)
         break;

      if(line[strlen(line)-1] == '\n')
         line[strlen(line)-1]  = '\0';

      if(line[strlen(line)-1] == '\r')
         line[strlen(line)-1]  = '\0';

      stradd(header, line);

      if(debug)
      {
         fprintf(fdebug, "%s\n", line);
	 fflush(fdebug);
      }
   }

   if(debug)
   {
      fprintf(fdebug, "\n");
      fflush(fdebug);
   }


   wcs = wcsinit(header);

   if(wcs == (struct WorldCoor *)NULL)
   {
      printf("[struct stat=\"ERROR\", msg=\"Output wcsinit() failed.\"]\n");
      fflush(stdout);
      exit(1);
   }

   naxis1 = wcs->nxpix;
   naxis2 = wcs->nypix;

   if(wcs->syswcs == WCS_J2000)
   {
      sys   = EQUJ;
      epoch = 2000.;

      if(wcs->equinox == 1950.)
         epoch = 1950;
   }
   else if(wcs->syswcs == WCS_B1950)
   {
      sys   = EQUB;
      epoch = 1950.;

      if(wcs->equinox == 2000.)
         epoch = 2000;
   }
   else if(wcs->syswcs == WCS_GALACTIC)
   {
      sys   = GAL;
      epoch = 2000.;
   }
   else if(wcs->syswcs == WCS_ECLIPTIC)
   {
      sys   = ECLJ;
      epoch = 2000.;

      if(wcs->equinox == 1950.)
      {
         sys   = ECLB;
         epoch = 1950.;
      }
   }
   else
   {
      sys   = EQUJ;
      epoch = 2000.;
   }


   if(debug)
   {
      fprintf(fdebug, "DEBUG> Template WCS initialized\n\n");
      fflush(fdebug);
   }

   fclose(fp);

   return(0);
}



int stradd(char *header, char *card)
{
   int i, hlen, clen;

   hlen = strlen(header);
   clen = strlen(card);

   for(i=0; i<clen; ++i)
      header[hlen+i] = card[i];

   if(clen < 80)
      for(i=clen; i<80; ++i)
         header[hlen+i] = ' ';

   header[hlen+80] = '\0';

   return(strlen(header));
}



/******************************/
/*                            */
/*  Print out general errors  */
/*                            */
/******************************/

int printError(char *msg)
{
   printf("[struct stat=\"ERROR\", msg=\"%s\"]\n", msg);
   exit(1);
}



/**********************************************************/
/*                                                        */
/*  Connect to lookup service and find the desired RA,Dec */
/*                                                        */
/**********************************************************/

int lookup(char *mosaicCenter, double *ra, double *dec)
{
   int    i, j, rc, socket, port, count;
  
   char   line      [MAXSTR];
   char   request   [MAXSTR];
   char   base      [MAXSTR];
   char   constraint[MAXSTR];
   char   server    [MAXSTR];

   char  *objStr, *ptr, *endPtr;

   char   result    [4096];

   strcpy(server, "irsa.ipac.caltech.edu");

   port = 80;

   strcpy(base, "/cgi-bin/Oasis/Lookup/nph-lookup?");

   objStr = url_encode(mosaicCenter);

   sprintf(constraint, "objstr=%s", objStr);


   /* Connect to the port on the host we want */

   socket = tcp_connect(server, port);
  

   /* Send a request for the file we want */

   sprintf(request, "GET %s%s HTTP/1.1\r\nHOST: %s:%d\r\n\r\n",
      base, constraint, server, port);

   if(debug)
   {
      fprintf(fdebug, "lookup(\"%s\") -> ", mosaicCenter);
      fflush(fdebug);
   }

   send(socket, request, strlen(request), 0);


   /* Read the data coming back */

   count = 0;
   j     = 0;

   while(1)
   {
      for(i=0; i<MAXSTR; ++i)
         line[i] = '\0';

      rc = recv(socket, line, 1, 0);

      if(rc <= 0)
         break;

      for(i=0; i<rc; ++i)
      {
	 result[j] = line[i];
	 ++j;
      }

      count += rc;
   }

   result[j] = '\0';


   if(count == 0)
   {
      printf("[struct stat=\"ERROR\", msg=\"No return message from object lookup.\"]\n");
      fflush(stdout);
      exit(1);
   }


   /* Check for errors */

   ptr = strstr(result, "\"error\"");

   if(ptr)
   {
      printf("[struct stat=\"ERROR\", msg=\"Invalid location specification or object name.\"]\n");
      fflush(stdout);
      exit(1);
   }


   /* Find the RA */

   endPtr = result + strlen(result);

   ptr = strstr(result, "\"lon\"");

   if(!ptr)
   {
      printf("[struct stat=\"ERROR\", msg=\"Can't find RA in lookup return.\"]\n");
      fflush(stdout);
      exit(1);
   }

   while(*ptr != '>' && ptr < endPtr)
      ++ptr;

   if(ptr >= endPtr)
   {
      printf("[struct stat=\"ERROR\", msg=\"Can't find RA in lookup return.\"]\n");
      fflush(stdout);
      exit(1);
   }

   ++ptr;

   *ra = atof(ptr);


   /* Find the Dec */

   ptr = strstr(result, "\"lat\"");

   if(!ptr)
   {
      printf("[struct stat=\"ERROR\", msg=\"Can't find Dec in lookup return.\"]\n");
      fflush(stdout);
      exit(1);
   }

   while(*ptr != '>' && ptr < endPtr)
      ++ptr;

   if(ptr >= endPtr)
   {
      printf("[struct stat=\"ERROR\", msg=\"Can't find Dec in lookup return.\"]\n");
      fflush(stdout);
      exit(1);
   }

   ++ptr;

   *dec = atof(ptr);

   if(debug)
   {
      fprintf(fdebug, "  (%.6f,%.6f)\n\n", *ra, *dec);
      fflush(fdebug);
   }

   return(0);
}




/***********************************************/
/* This is the basic "make a connection" stuff */
/***********************************************/

int tcp_connect(char *hostname, int port)
{
   int                 sock_fd;
   struct hostent     *host;
   struct sockaddr_in  sin;


   if((host = gethostbyname(hostname)) == NULL) 
   {
      printf("[struct stat=\"ERROR\", msg=\"Couldn't find host %s\"]\n", hostname);
      fflush(stdout);
      return(0);
   }

   if((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
   {
      printf("[struct stat=\"ERROR\", msg=\"Couldn't create socket()\"]\n");
      fflush(stdout);
      return(0);
   }

   sin.sin_family = AF_INET;
   sin.sin_port = htons(port);
   bcopy(host->h_addr_list[0], &sin.sin_addr, host->h_length);

   if(connect(sock_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
   {
      printf("[struct stat=\"ERROR\", msg=\"%s: connect failed.\"]\n", hostname);
      fflush(stdout);
      return(0);
   }

   return sock_fd;
}



/**************************************/
/* This routine URL-encodes a string  */
/**************************************/

static unsigned char hexchars[] = "0123456789ABCDEF";

char *url_encode(char *s)
{
   int      len;
   register int i, j;
   unsigned char *str;

   len = strlen(s);

   str = (unsigned char *) malloc(3 * strlen(s) + 1);

   j = 0;

   for (i=0; i<len; ++i)
   {
      str[j] = (unsigned char) s[i];

      if (str[j] == ' ')
      {
         str[j] = '+';
      }
      else if ((str[j] < '0' && str[j] != '-' && str[j] != '.') ||
               (str[j] < 'A' && str[j] > '9')                   ||
               (str[j] > 'Z' && str[j] < 'a' && str[j] != '_')  ||
               (str[j] > 'z'))
      {
         str[j++] = '%';

         str[j++] = hexchars[(unsigned char) s[i] >> 4];

         str[j]   = hexchars[(unsigned char) s[i] & 15];
      }

      ++j;
   }

   str[j] = '\0';

   return ((char *) str);
}


