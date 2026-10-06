// Module Assignment: Colossus Airlines Files Persistence & AI Corruption Fuzzing


// Objective: you will extend your C program to achieve durable file persistence using C File I/O (fopen, fread, fwrite, fclose), and leverage AI to 
// generate corrupt data files to stress-test your file-loading and recovery mechanics

#include <stdio.h>
#include <string.h>


#define LINE_SIZE 50


struct seat {
    int seatID;
    int assigned;
    char lastname[50];
    char firstname[50];
};


// returns -1 on end of file, 1 if the line was too long, 0 if ok
int read_line(char *buf, int size)
{
    int len;
    int c;

    if (fgets(buf, size, stdin) == NULL)
        return -1;

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
    {
        buf[len - 1] = '\0';
        return 0;
    }
        c = getchar();
    if (c == EOF)
        return 0;
    while (c != '\n' && c != EOF)
        c = getchar();
    return 1;
}


// returns the menu letter, 0 if invalid, EOF if input ended
int get_choice(void)
{
    char line[LINE_SIZE];
    int status = read_line(line, LINE_SIZE);

    if (status == -1)
        return EOF;
    if (status == 1 || strlen(line) != 1)
        return 0;
    return line[0];
}


// A blank line at any prompt cancels
void assign_seat(struct seat flight[])
{
    char line[LINE_SIZE];
    int num;

    printf("Seat number (blank to cancel): ");
    if (read_line(line, LINE_SIZE) == -1)
        return;
    if (line[0] == '\0')
        return;
    if (sscanf(line, "%d", &num) != 1 || num < 1 || num > 24)
    {
        printf("Invalid seat number.\n");
        return;
    }
    if (flight[num - 1].assigned)
    {
        printf("Seat %d is already assigned.\n", num);
        return;
    }

    printf("Last name (blank to cancel): ");
    read_line(flight[num - 1].lastname, 50);
    if (flight[num - 1].lastname[0] == '\0')
        return;

    printf("First name (blank to cancel): ");
    read_line(flight[num - 1].firstname, 50);
    if (flight[num - 1].firstname[0] == '\0')
    {
        flight[num - 1].lastname[0] = '\0';
        return;
    }

    flight[num - 1].assigned = 1;
    printf("Seat %d assigned.\n", num);
}


void count_empty(struct seat flight[])
{
    int i;
    int count = 0;

    for (i = 0; i < 24; i++)
        if (!flight[i].assigned)
            count++;
    printf("Empty seats: %d\n", count);
}


void list_empty(struct seat flight[])
{
    int i;

    printf("Empty seats:");
    for (i = 0; i < 24; i++)
        if (!flight[i].assigned)
            printf(" %d", flight[i].seatID);
    printf("\n");
}


void list_alphabetical(struct seat flight[])
{
    int order[24];
    int n = 0;
    int i, j, temp;

    for (i = 0; i < 24; i++)
        if (flight[i].assigned)
            order[n++] = i;

    if (n == 0)
    {
        printf("No seats assigned.\n");
        return;
    }

    // bubble sort the assigned seats by last name
    for (i = 0; i < n - 1; i++)
        for (j = 0; j < n - 1 - i; j++)
            if (strcmp(flight[order[j]].lastname, flight[order[j + 1]].lastname) > 0)
            {
                temp = order[j];
                order[j] = order[j + 1];
                order[j + 1] = temp;
            }

    for (i = 0; i < n; i++)
        printf("Seat %d: %s, %s\n", flight[order[i]].seatID,
               flight[order[i]].lastname, flight[order[i]].firstname);
}


void delete_seat(struct seat flight[])
{
    char line[LINE_SIZE];
    int num;

    printf("Seat number to delete (blank to cancel): ");
    if (read_line(line, LINE_SIZE) == -1)
        return;
    if (line[0] == '\0')
        return;
    if (sscanf(line, "%d", &num) != 1 || num < 1 || num > 24)
    {
        printf("Invalid seat number.\n");
        return;
    }
    if (!flight[num - 1].assigned)
    {
        printf("Seat %d is not assigned.\n", num);
        return;
    }

    flight[num - 1].assigned = 0;
    flight[num - 1].lastname[0] = '\0';
    flight[num - 1].firstname[0] = '\0';
    printf("Seat %d assignment deleted.\n", num);
}



// Second-level menu

void flight_menu(struct seat flight[])
{
    int choice;

    do {
        printf("\na) Show number of empty seats\n");
        printf("b) Show list of empty seats\n");
        printf("c) Show alphabetical list of seats\n");
        printf("d) Assign a customer to a seat\n");
        printf("e) Delete a seat assignment\n");
        printf("f) Return to Main Menu\n");
        printf("Choice: ");
        choice = get_choice();

        switch (choice) {
        case 'a': count_empty(flight); break;
        case 'b': list_empty(flight); break;
        case 'c': list_alphabetical(flight); break;
        case 'd': assign_seat(flight); break;
        case 'e': delete_seat(flight); break;
        case 'f': break;
        default:  printf("Invalid choice.\n");
        }
    } while (choice != 'f' && choice != EOF);
}


// Two arrays of struct seat for both inbound and outbound
// Write a for loop to initialize every seat for both inbound and outbound seats instead of writing it manually




void init_flight(struct seat flight[])

{
    int i; 

    for (i = 0; i < 24; i++){
        flight[i].seatID = i + 1;
        flight[i].assigned = 0;
        flight[i].lastname[0] = '\0';
        flight[i].firstname[0] = '\0';

    }

}


// returns 1 if the name is ok, 0 if it is bad
int name_ok(char name[])
{
    int j;

    for (j = 0; j < 50; j++) {
        if (name[j] == '\0')
            return 1;
        if (name[j] < 32 || name[j] > 126)
            return 0;
    }
    return 0;
}


// returns 1 if the flight is ok, 0 if it is bad
int check_flight(struct seat flight[])
{
    int i;

    for (i = 0; i < 24; i++) {
        if (flight[i].seatID != i + 1)
            return 0;
        if (flight[i].assigned != 0 && flight[i].assigned != 1)
            return 0;
        if (name_ok(flight[i].lastname) == 0)
            return 0;
        if (name_ok(flight[i].firstname) == 0)
            return 0;
    }
    return 1;
}


void load_data(struct seat outbound[], struct seat inbound[])
{
    FILE *fp;
    int extra;

    init_flight(outbound);
    init_flight(inbound);

    fp = fopen("flight_data.dat", "rb");
    if (fp == NULL) {
        printf("No saved data found. Starting with empty flights.\n");
        return;
    }

    if (fread(outbound, sizeof(struct seat), 24, fp) != 24 ||
        fread(inbound, sizeof(struct seat), 24, fp) != 24) {
        printf("File is too short. Starting with empty flights.\n");
        init_flight(outbound);
        init_flight(inbound);
        fclose(fp);
        return;
    }

    extra = fgetc(fp);
    fclose(fp);
    if (extra != EOF) {
        printf("File is too big. Starting with empty flights.\n");
        init_flight(outbound);
        init_flight(inbound);
        return;
    }

    if (check_flight(outbound) == 0 || check_flight(inbound) == 0) {
        printf("File has bad data. Starting with empty flights.\n");
        init_flight(outbound);
        init_flight(inbound);
        return;
    }

    printf("Saved data loaded.\n");
}






int save_data(struct seat outbound[], struct seat inbound[])
{
    FILE *fp;
    size_t n;

    fp = fopen("flight_data.dat", "wb");
    if (fp == NULL) {
        printf("Could not open file for saving.\n");
        return -1;
    }

    n = fwrite(outbound, sizeof(struct seat), 24, fp);
    if (n != 24) {
        printf("Error saving outbound flight.\n");
        fclose(fp);
        return -1;
    }

    n = fwrite(inbound, sizeof(struct seat), 24, fp);
    if (n != 24) {
        printf("Error saving inbound flight.\n");
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0) {
        printf("Error closing file.\n");
        return -1;
    }

    return 0;
}















int main(void)
{
    struct seat outbound[24];
    struct seat inbound[24];
    int choice;
    int i;


    load_data(outbound, inbound);


    do {
        printf("\na) Outbound Flight\n");
        printf("b) Inbound Flight\n");
        printf("c) Quit\n");
        printf("Choice: ");
        choice = get_choice();

        switch (choice) {
        case 'a': flight_menu(outbound); break;
        case 'b': flight_menu(inbound);  break;
        case 'c': printf("Goodbye.\n");  break;
        default:  printf("Invalid choice.\n");
        }

        
    } while (choice != 'c' && choice != EOF);
    

    save_data(outbound, inbound);


    return 0;
}