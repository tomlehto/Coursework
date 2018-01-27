/* Data structures and algoritms course project
Tomi Lehto 2018 
Requires the bst-library (with some modifications) given by the lecturer
Finds the 100 most common words of a text file
and prints them and their occurrences to output.txt
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bst.h"

#define MAX_WORDS 50000
#define LINE_BUFFER_SIZE 2600 

//Converts a string to upper case
void strToUpper(char *str){
    while(*str != '\0'){
        *str = toupper(*str);
        str++;
    }
}
// Compare function for qsort
int comparefunc(const void *p, const void *q){
    int l = *((arraynode*) p)->count;
    int r = *((arraynode*) q)->count;
    return (r-l);
}

int main(int argc, char* argv[]){
    //init  BST
    bst coll;
    coll.root = 0;
    
    //init time variables
    double totaltime;
    clock_t start, end;
    char *ch;
    
    // Delimiter characters for strtok
    const char delim[] = " \t\r\n-_.,:;!?()+<>|#%&=~^/*\"0123456789[]{}";
    
    char *word;
    long int uniqueWords = 0;//Number of unique words
    pbstnode x;//Temporary BST-node pointer
    char buff[LINE_BUFFER_SIZE];//Line buffer
    

    //Init array in which the words are stored and sorted later
    arraynode *wordarray = malloc(MAX_WORDS * sizeof(arraynode));
    
    //Init file variable
    FILE *fd = NULL;

    if(argv[1] == NULL){
        printf("Usage: ./mostCommonWords FILE\n");
        return 0;
    }
    fd = fopen(argv[1], "r");
    if(fd == NULL){
        printf("Error opening file!\n");
        return 1;
    }
    //Start clock
    start = clock();

    //Read first line from file
    ch = fgets(buff, sizeof(buff), fd);
    while(ch != NULL){
        //Get first word of line
        word = strtok(ch, delim);
        
        while(word != NULL){
            strToUpper(word);
            //Insert to BST  
            x = bst_insert(&coll, word);
            
            if(x != 0){
                /*If a unique word is found                       
                insert it to the array*/
                wordarray[uniqueWords].word = x->data;
                wordarray[uniqueWords].count = &(x->count);
                
                //Increment unique word counter
                uniqueWords++; 
            }
            //Get next word         
            word = strtok(NULL, delim);
        }    
        //Reset line buffer
        memset(buff, 0, sizeof(buff));
        //read another line
        ch = fgets(buff, sizeof(buff), fd);
    }
    //Sort array by number of occurrences with qsort 
    qsort(wordarray, uniqueWords, sizeof(arraynode), comparefunc);
    
    //End clock and calculate and print consumed time
    end = clock();
    totaltime = (double) (end-start)/CLOCKS_PER_SEC;
    printf("Consumed time: %f seconds\n", totaltime);
        
    //Close source file
    fclose(fd);
    
    //Open destination file
    fd = fopen("output.txt", "w");
    
    //Write data to output file
    fprintf(fd, "The 100 most common words in %s:\n", argv[1]);
    fprintf(fd, "WORD              NUMBER OF OCCURRENCES\n"); 
    for(int j = 0; j<100;j++){
        fprintf(fd, "%-17s %d\n", wordarray[j].word, *wordarray[j].count);
    }    
    
    printf("Successfully wrote to output.txt\n");
    //Delete BST
    bst_delete(coll.root);
	coll.root=0;
	
	//Delete array
	free(wordarray);
	
    //Close file
    fclose(fd);
    
    return(0);
}

