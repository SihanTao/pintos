/* halt.c

   Simple program to test whether running a user program works.
 	
   Just invokes a system call that shuts down the OS. */

#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf("inside halt\n");
  halt ();
  /* not reached */
}
