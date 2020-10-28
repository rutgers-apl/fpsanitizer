/* mesa4.c */
     
     
/*
 * Mesa SPEC benchmark
 *
 * Brian Paul  brianp@elastic.avid.com 
 *
 *
 * This program renders images of a function of the form z = f(x, y). 
 *
 * The rendering of the function is drawn with filled, smooth-shaded, lit 
 * triangle strips.
 * The surface's geometry is computed only once. 
 *
 * Tunable parameters:
 *    WIDTH, HEIGHT = size of image to generate, in pixels. 
 *
 * Command line usage/parameters:
 *        mesa4 [-frames n] [-meshfile filename] [-ppmfile filename] 
 * where
 *    -frames specifies number of frames of animation (1000 is default) 
 *    -meshfile specifies name of input mesh ("mesa.mesh" is default)
 *    -ppmfile specifies the name of output ppm image file ("mesa.ppm" 
 *           is default)
 *
 *
 * Version history:
 *    mesa1.c   - original version sent to Rahul.
 *    mesa2.c   - updated to use a 1-D texture for constant lines of Z. 
 *    mesa3.c   - read mesh from file instead of generate during init. 
 *    mesa4.c   - write ASCII PPM file intead of binary.
 */

/*
 * wwlk Revision history - minor stuff...
 *		- slightly smaller image so it doesn't clip to window borders.
 *		- rotate ends at zero Yrot. (Help validation I hope?)
 *		- limit Yrot to 0-360. (More help for validation.)
 *		- change to three lights! (Increase fp content of workload.)
 *		- Increase window to 1024x1024.
 */
/*
 * Rahul - 04/13/99
 * Changed image size from 1024 * 1024 to 1280 * 1024
 */
     
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "GL/osmesa.h"
#include "macros.h"     
     
/*
 * The frame buffer:
 */

/*
 * wwlk
 *
 * This is another BIG knob in this mesa
 * benchmark, again order n^2.
 *
 * We have to be more sensitive this time, so....
 * Let's just increase the frame buffer size a bit.
 *
 * There's nothing wrong with 800x800, it is a
 * common window size, but with the mesh size
 * going up, we should probably up the frame buffer
 * just a bit as well.  Not as much!
 *
 * We'll only bring it up to 1024x1024.  This
 * is another common square graphics surface
 * size.
 *
 * SPEC OSG folks, yup, 1024x1024 sets up
 * some interesting power of two issues.
 * If this is a problem, 900x900 or 960x960
 * are also common sizes.
 *
 */

/* #define WIDTH 800								   wwlk */
/* #define HEIGHT 800								   wwlk */
/* #define WIDTH 1024									wwlk */
#define WIDTH 1280									/* wwlk */
#define HEIGHT 1024									/* wwlk */

     
/*
 * The object we're rendering:
 */
static int NumRows, NumColumns;
static GLfloat *SurfaceV;
static GLfloat *SurfaceN;
static float Xmin = -10.0, Xmax = 10.0; 
static float Ymin = -10.0, Ymax = 10.0;
     
     
/*
 * Viewing parameters:
 */

/*
 * wwlk
 *
 * Note, Yrot will get clobbered in Render later!
 */

static GLfloat Xrot = -35.0, Yrot = 40.0;
     
     
     
/*
 * Read quadmesh from given file.
 */
static void ReadMesh(const char *filename) 
{
   int i, j;
   FILE *f = fopen(filename, "r");
   if (!f) {
      printf("Error: couldn't open input mesh file: %s\n", filename); 
      exit(1);
   }
     
   fscanf(f, "%d %d\n", &NumRows, &NumColumns); 
   if (NumRows < 2) {
      printf("Error: number of mesh rows invalid\n"); 
      exit(1);
   }
   if (NumColumns < 2) {
      printf("Error: number of mesh columns invalid\n"); 
      exit(1);
   }
     
   /* Allocate storage for mesh vertices and normal vectors */
   SurfaceV = (GLfloat *) malloc(NumRows * NumColumns * 3 * sizeof(GLfloat)); 
   SurfaceN = (GLfloat *) malloc(NumRows * NumColumns * 3 * sizeof(GLfloat)); 
   if (!SurfaceV || !SurfaceN) {
      printf("Error: unable to allocate memory for mesh data\n"); 
      exit(1);
   }
     
   for (i=0; i<NumRows; i++) {
      for (j=0; j<NumColumns; j++) {
      int k = (i * NumColumns + j) * 3;
      float vx, vy, vz, nx, ny, nz;
      fscanf(f, "%f %f %f  %f %f %f\n", &vx, &vy, &vz, &nx, &ny, &nz); 
      SurfaceV[k+0] = vx;
      SurfaceV[k+1] = vy;
      SurfaceV[k+2] = vz;
      SurfaceN[k+0] = nx;
      SurfaceN[k+1] = ny;
      SurfaceN[k+2] = nz;
      }
   }
     
   /* Mesh read successfully */
   fclose(f);
}
     
     
     
     
/*
 * Draw the surface mesh.
 */
static void DrawMesh_Slice( int i )
{
  int j;

  glEnd();
}

static void DrawMesh( void )
{
   int i, j;

   for (i=0; i < NumRows-1; i++) {
     glBegin(GL_TRIANGLE_STRIP);
     for (j=0; j < NumColumns; j++) {
       int k0 = ( i    * NumColumns + j) * 3; 
       int k1 = ((i+1) * NumColumns + j) * 3;
       glNormal3fv(SurfaceN + k0);
       glVertex3fv(SurfaceV + k0);
       glNormal3fv(SurfaceN + k1);
       glVertex3fv(SurfaceV + k1);
     }
     DrawMesh_Slice(i);
   }
}
     
/* This is to write out some intermediate output so as to ensure that
 *   people do all the work
 */     
static void SPECWriteIntermediateImage( FILE *fip, FILE *fop, int width, int height, const void *buffer, int frame_count)
{
      int i, x, y;
      GLubyte *ptr = (GLubyte *) buffer;
      int counter = 0;


      if (fip)
	{
	  fscanf(fip, "%d %d", &x, &y); 
	  y = y % height;
	  x = x % width; 
	  i = (y*width + x) * 4;
	}
      if (fop)
	{
	  fprintf(fop, "%d %d %d %d\n", i, ptr[i], ptr[i+1], ptr[i+2]); 
	}
}
    
     
/*
 * This is the main rendering function.  We simply render the surface 
 * 'frames' times with different rotations.
 */
static void Render( int frames, int width, int height, const void *buffer  )
{
   int i;
   FILE *fip = fopen("numbers", "r"); 
   FILE *fop = fopen("mesa.log", "w"); 
   /* wwlk
    *
	* OK, let's make verification a *bit* easier.
	*
	* If we rotate up from yrot=40degrees,
	* and then add (frames-1)*5.0 to yrot,
	* who knows where we'll end up.  Well,
	* we know exactly where we end up, but
	* it depends on how many frames.
	*
	* Instead, let's rotate so that the last
	* frame yrot = 0.0!  Gosh, this is so
	* obvious, I should have done it before.
	*
	*/

   Yrot = -5.0F*(float)(frames-1);								/* wwlk */
   
   for (i=0; i<frames; i++) {
      /* wwlk
	   *
	   * Limit Yrot to range 0-360
	   * 
	   * This is standard recommended practice in OpenGL
	   * progamming.  Remember, OpenGL specifies angles
	   * in degrees, not radians.  They'll get converted
	   * to radians by Mesa, and Mesa says PI=3.1415926.
	   *
	   *
	   */

	  if (Yrot > 360.0F) {
		  while (Yrot > 360.0F) {
			  Yrot = Yrot - 360.0F;								/* wwlk */
		  }
	  }
	  else if (Yrot < 0.0F) {									/* wwlk */
			while (Yrot < 0.0F ) {								/* wwlk */
				Yrot = Yrot + 360.0F;							/* wwlk */
			}
	  }															/* wwlk */
	  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 
      glPushMatrix();
      glRotatef(-Xrot, 1, 0, 0);
      glRotatef(Yrot, 0, 1, 0);
      glRotatef(-90, 1, 0, 0);
     
      SPECWriteIntermediateImage(fip, fop, width, height, buffer, i); 

      DrawMesh();
     
      glPopMatrix();
     
	 
      Yrot += 5.0F;												/* wwlk */
   }
}
     
     
/*
 * Called to setup the viewport, projection and viewing parameters. 
 */
static void Reshape( int width, int height ) 
{
   GLfloat w = (float) width / (float) height; 
   glViewport( 0, 0, width, height );
   glMatrixMode( GL_PROJECTION );
   glLoadIdentity();
/* glFrustum( -w, w, -1.0, 1.0, 5.0, 100.0 );			   wwlk */ 
   glFrustum( -w*1.1, w*1.1, -1.1, 1.1, 5.0, 100.0 );	/* wwlk */
   glMatrixMode( GL_MODELVIEW );
   glLoadIdentity();
   glTranslatef( 0.0, 0.0, -60.0 );
}
     
     
/*
 * Initialize library parameters.
 */
static void Init( void )
{
   GLint i;
   GLubyte texture[256];
   static GLfloat texPlane[4] = {0.0, 0.0, 0.1, 0.0}; 
   static GLfloat blue[4] = {0.2, 0.2, 1.0, 1.0}; 
   static GLfloat white[4] = {1.0, 1.0, 1.0, 1.0}; 

   /* wwlk
    *
	* OK, here's an arguable change.  Let's increase
	* the number of lights.  We'll up it to three
	* to get a slightly more theatrical look.
	*
	* We do this by using a grayred, a graygreen
	* and a grayblue light.  In other words,
	* build up a grey from three individual
	* lights, sort of like on stage.
	* We'll have the lights come from
	* slightly different directions.
	*
	* The diffuse lighting calculation will
	* hardly change at all.  The specular
	* lighting calculation will tend to look
	* a bit more dramatic - but just a bit.
	*
	* How common is this style of lighting?
	* Probably more common than it ought to be.
	* But honestly, not very.  However,
	* more than one light is quite common.
	* Which is why including another couple
	* of lights can be defended.
	*
	* But this is the only change that might be in
	* danger of approaching Glitz Buffer Overflow.
	* (John Judy Phil and Paula will get the
	* reference.)
	*/

   /* static GLfloat pos[4] = {0.0, 2.0, 5.0, 0.0};				   wwlk */
   static GLfloat posred[4] =    {0.0, 2.0, 5.0, 0.0};			/* wwlk */
   static GLfloat posgreen[4] =  {0.0, 2.5, 5.0, 0.0};			/* wwlk */
   static GLfloat posblue[4] =   {0.0, 2.0, 6.0, 0.0};			/* wwlk */

   /* static GLfloat gray[4] = {0.5, 0.5, 0.5, 1.0};			   wwlk */ 
   static GLfloat grayred[4] =   {0.43F, 0.125F, 0.0625F, 1.0F};/* wwlk */
   static GLfloat graygreen[4] = {0.0625F, 0.45F, 0.125F, 1.0F};/* wwlk */
   static GLfloat grayblue[4] =  {0.125F, 0.0625F, 0.47F, 1.0F};/* wwlk */
   glEnable(GL_LIGHTING);
   glEnable(GL_LIGHT0);
   glEnable(GL_LIGHT1);											/* wwlk */
   glEnable(GL_LIGHT2);											/* wwlk */

   glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, blue); 
   glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white); 
   glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 15.0);

   /* glLightfv(GL_LIGHT0, GL_POSITION, pos);					   wwlk */
   /* glLightfv(GL_LIGHT0, GL_DIFFUSE, gray);					   wwlk */
   /* glLightfv(GL_LIGHT0, GL_SPECULAR, gray);					   wwlk */

   glLightfv(GL_LIGHT0, GL_POSITION, posred);					/* wwlk */
   glLightfv(GL_LIGHT0, GL_DIFFUSE, grayred);					/* wwlk */
   glLightfv(GL_LIGHT0, GL_SPECULAR, grayred);			  /* wwlk 15-April-1999 */

   glLightfv(GL_LIGHT1, GL_POSITION, posgreen);					/* wwlk */
   glLightfv(GL_LIGHT1, GL_DIFFUSE, graygreen);					/* wwlk */
   glLightfv(GL_LIGHT1, GL_SPECULAR, graygreen);		  /* wwlk 15-April-1999 */
   
   glLightfv(GL_LIGHT2, GL_POSITION, posblue);					/* wwlk */
   glLightfv(GL_LIGHT2, GL_DIFFUSE, grayblue);					/* wwlk */
   glLightfv(GL_LIGHT2, GL_SPECULAR, grayblue);			  /* wwlk 15-April-1999 */

   glEnable(GL_DEPTH_TEST);
     
   /* Setup 1-D texture map to render lines of constant Z */ 
   for (i=0; i<256; i++) {
      if ((i % 16) < 1) {
         texture[i] = 0;
      }
      else {
         texture[i] = 255;
      }
   }
   glTexImage1D(GL_TEXTURE_1D, 0, 1, 256, 0,
                GL_LUMINANCE, GL_UNSIGNED_BYTE, texture);
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
   glEnable(GL_TEXTURE_1D);
     
   glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR); 
   glTexGenfv(GL_S, GL_OBJECT_PLANE, texPlane); 
   glEnable(GL_TEXTURE_GEN_S);
     
   Reshape(WIDTH, HEIGHT);
}
     
     
/*
 * Write current image in Buffer to the named file. 
 */
static void WriteImage( const char *filename, int width, int height,
                        const void *buffer )
{
   FILE *f = fopen( filename, "w" );
   if (f) {
      int i, x, y;
      GLubyte *ptr = (GLubyte *) buffer;
      int counter = 0;
      fprintf(f, "P3\n");
      fprintf(f, "# ascii ppm file created by %s\n", "SPEC mesa177"); 
      fprintf(f, "%i %i\n", width, height);
      fprintf(f, "255\n");
      for (y=HEIGHT-1; y>=0; y--) {
         for (x=0; x<WIDTH; x++) {
            i = (y*WIDTH + x) * 4;
            fprintf(f, " %d %d %d", ptr[i], ptr[i+1], ptr[i+2]); 
            counter++;
            if (counter % 5 == 0)
               fprintf(f, "\n");
         }
      }
      fclose(f);
   }
}
     
     
int main(int argc, char *argv[])
{
   OSMesaContext ctx;
   void *buffer;
   int frames = 1000;
   char *ppmFile = "mesa.ppm";
   char *meshFile = "mesa.mesh";
   char *intermediateResultsFile = "mesa.log"; 
   int i;

   for (i=1; i<argc; i++) {
      if (strcmp(argv[i],"-frames")==0) {
      if (i+1 >= argc) {
         printf("Error:  missing argument after -frames\n"); 
         return 1;
      }
      frames = atoi(argv[i+1]);
      i++;
      if (frames <= 0) {
         printf("Error:  number of frames must be >= 1\n"); 
         return 1;
      }
      }
      else if (strcmp(argv[i],"-ppmfile")==0) { 
      if (i+1 >= argc) {
         printf("Error:  missing argument after -ppmfile\n"); 
         return 1;
      }
      ppmFile = argv[i+1];
      i++;
      }
      else if (strcmp(argv[i],"-meshfile")==0) { 
      if (i+1 >= argc) {
         printf("Error:  missing argument after -meshfile\n"); 
         return 1;
      }
      meshFile = argv[i+1];
      i++;
      }
      else {
      printf("Error:  unexpect command line parameter: %s\n", argv[i]); 
      return 1;
      }
   }
     
   /* Create an RGBA-mode context */
   ctx = OSMesaCreateContext( GL_RGBA, NULL );
     
   /* Allocate the image buffer */
   buffer = malloc( WIDTH * HEIGHT * 4 );
     
   /* Bind the buffer to the context and make it current */ 
   OSMesaMakeCurrent( ctx, buffer, GL_UNSIGNED_BYTE, WIDTH, HEIGHT );
     
   /* Initialize GL stuff */
   Init();
     
   /* Load surface mesh */
   ReadMesh(meshFile);
     
   Render( frames, WIDTH, HEIGHT, buffer );
     
   /* If an optional output filename is given write a PPM image file */ 
   WriteImage( ppmFile, WIDTH, HEIGHT, buffer );
     
   /* free the image buffer */
   free( buffer );
     
   /* destroy the context */
   OSMesaDestroyContext( ctx );
     
   return 0;
}
