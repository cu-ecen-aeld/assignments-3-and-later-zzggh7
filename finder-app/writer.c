#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    const char *filename = "output.txt";
  
    openlog(NULL,0,LOG_USER);
    
    printf("%s\n%s\n%s\n",argv[0], argv[1], argv[2]);
 
    if(3 != argc) {
        fprintf(stderr, "Invalid number of parametrs\n");
        fprintf(stderr, "You need to provide 2 parameters:\n");
        fprintf(stderr, "1) A filename\n");
        fprintf(stderr, "2) Text string to be written to the file\n");
        syslog(LOG_ERR, "Invalid number of arguments %d", argc);
        return 1;
    }
    else
    {
        filename = argv[1];
    }
    
    printf("File to open: %s\n", filename);

    FILE *file = fopen(filename, "w");
  
    if (file == NULL) 
    {
        perror("Error opening the file");
        syslog(LOG_ERR,"LOG: Can't opent the file %s", filename);
        return 1;
    }
    else 
    {
        printf("File '%s' opened successfully.\n", filename);
        fprintf(file,"%s",argv[2]);
        syslog(LOG_DEBUG, "Writing %s to file %s", argv[2], filename);
        fclose(file);
    }

    return 0;
}