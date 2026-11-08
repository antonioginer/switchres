#include <stdio.h>
#include <stdlib.h>
#include <switchres/switchres_wrapper.h>

int main()
{
	printf("Switchres version: %s\n" , sr_get_version());
}
