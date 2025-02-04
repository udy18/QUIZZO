#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define MAX_CLIENTS 2
#define BUFFER_SIZE 1024
#define QUESTIONS_PER_CATEGORY 3

typedef struct {
    char question[256];
    char options[4][64];
    int correct_answer;
    char category[32];
} Question;

typedef struct {
    int socket;
    char name[32];
    int score;
    int dice_roll;
    int answered_questions;
} Client;

Question questions[] = {
    // Science
    {"What planet is known as the Red Planet?", 
     {"Venus", "Mars", "Jupiter", "Saturn"}, 1, "Science"},
    {"What is the chemical symbol for Gold?", 
     {"Au", "Ag", "Fe", "Cu"}, 0, "Science"},
    {"What is the chemical formula for water?", 
     {"H2O", "CO2", "O2", "N2"}, 0, "Science"},
    
    // History
    {"Who was the first President of the USA?", 
     {"Jefferson", "Washington", "Adams", "Lincoln"}, 1, "History"},
    {"In which year did World War II end?", 
     {"1944", "1945", "1946", "1947"}, 1, "History"},
    {"Who discovered America?", 
     {"Columbus", "Vespucci", "Magellan", "Cabot"}, 0, "History"},
    
    // Geography
    {"What is the capital of Japan?", 
     {"Seoul", "Beijing", "Tokyo", "Bangkok"}, 2, "Geography"},
    {"Which is the largest ocean?", 
     {"Atlantic", "Indian", "Arctic", "Pacific"}, 3, "Geography"},
    {"Which country is known as the Land of the Rising Sun?", 
     {"South Korea", "China", "Japan", "India"}, 2, "Geography"}
};

Client clients[MAX_CLIENTS];
int client_count = 0;
char categories[][32] = {"Science", "History", "Geography"};
int num_categories = 3;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
char current_category[32] = "";
int current_question_index = 0;
bool game_started = false;

void send_to_all(char *message) {
    for (int i = 0; i < client_count; i++) {
        send(clients[i].socket, message, strlen(message), 0);
    }
}

int roll_dice() {
    return (rand() % 6) + 1;
}

void broadcast_question() {
    static int category_question_indices[3] = {0, 1, 2};
    int q_index = -1;
    
    for (int i = 0; i < sizeof(questions)/sizeof(questions[0]); i++) {
        if (strcmp(questions[i].category, current_category) == 0) {
            for (int j = 0; j < 3; j++) {
                if (category_question_indices[j] != -1) {
                    q_index = i + category_question_indices[j];
                    category_question_indices[j] = -1;
                    break;
                }
            }
            break;
        }
    }
    
    if (q_index != -1) {
        Question* current_q = &questions[q_index];
        char question_text[BUFFER_SIZE];
        snprintf(question_text, BUFFER_SIZE, 
                "\nQuestion:\n%s\n1. %s\n2. %s\n3. %s\n4. %s\nYour answer (1-4): ",
                current_q->question,
                current_q->options[0], current_q->options[1],
                current_q->options[2], current_q->options[3]);
        send_to_all(question_text);
    }
}

void display_scoreboard() {
    char scoreboard[BUFFER_SIZE];
    snprintf(scoreboard, BUFFER_SIZE, 
            "\n=== FINAL SCOREBOARD ===\n"
            "%s: %d points\n"
            "%s: %d points\n"
            "=====================\n",
            clients[0].name, clients[0].score,
            clients[1].name, clients[1].score);
    
    if (clients[0].score > clients[1].score) {
        char winner[BUFFER_SIZE];
        snprintf(winner, BUFFER_SIZE, "\n🏆 %s wins! 🏆\n", clients[0].name);
        strcat(scoreboard, winner);
    } else if (clients[1].score > clients[0].score) {
        char winner[BUFFER_SIZE];
        snprintf(winner, BUFFER_SIZE, "\n🏆 %s wins! 🏆\n", clients[1].name);
        strcat(scoreboard, winner);
    } else {
        strcat(scoreboard, "\nIt's a tie!\n");
    }
    
    send_to_all(scoreboard);
    send_to_all("\nThank you for playing! Goodbye.\n");
}

// Global variable to track synchronized question progress
int total_answered_questions = 0;

void check_answer(int answer, Client* client) {
    int q_index = -1;
    Question* current_q = NULL;
    
    for (int i = 0; i < sizeof(questions)/sizeof(questions[0]); i++) {
        if (strcmp(questions[i].category, current_category) == 0) {
            current_q = &questions[i];
            break;
        }
    }
    
    if (current_q && answer == current_q->correct_answer + 1) {
        client->score++;
        char result_msg[BUFFER_SIZE];
        snprintf(result_msg, BUFFER_SIZE, "%s answered correctly!\n", client->name);
        send_to_all(result_msg);
    } else {
        char result_msg[BUFFER_SIZE];
        snprintf(result_msg, BUFFER_SIZE, "%s answered incorrectly.\n", client->name);
        send_to_all(result_msg);
    }

    client->answered_questions++;
    total_answered_questions++;

    // Check if both players have answered the current question
    if (total_answered_questions % 2 == 0) {
        if (client->answered_questions < QUESTIONS_PER_CATEGORY) {
            send_to_all("\nMoving to next question...\n");
            broadcast_question();
        } else if (clients[0].answered_questions >= QUESTIONS_PER_CATEGORY && 
                   clients[1].answered_questions >= QUESTIONS_PER_CATEGORY) {
            display_scoreboard();
            // Reset for potential new game
            total_answered_questions = 0;
        }
    }
}
void *handle_client(void *arg) {
    Client *client = (Client *)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;

    send(client->socket, "Enter your name: ", 16, 0);
    bytes_received = recv(client->socket, client->name, 31, 0);
    client->name[bytes_received - 1] = '\0';

    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "\nWelcome %s!\n", client->name);
    send(client->socket, welcome, strlen(welcome), 0);

    if (client_count < 2) {
        send(client->socket, "Waiting for other player...\n", 27, 0);
    } else if (client_count == 2 && !game_started) {
        game_started = true;
        send_to_all("\nBoth players connected! Rolling dice...\n");
        
        for (int i = 0; i < client_count; i++) {
            clients[i].dice_roll = roll_dice();
            char roll_msg[BUFFER_SIZE];
            snprintf(roll_msg, BUFFER_SIZE, "%s rolled %d!\n", 
                    clients[i].name, clients[i].dice_roll);
            send_to_all(roll_msg);
        }

        int chooser = clients[0].dice_roll >= clients[1].dice_roll ? 0 : 1;
        char choice_msg[BUFFER_SIZE];
        snprintf(choice_msg, BUFFER_SIZE, "\n%s gets to choose the category!\n", 
                clients[chooser].name);
        send_to_all(choice_msg);

        char menu[BUFFER_SIZE] = "\nChoose a category:\n";
        for (int i = 0; i < num_categories; i++) {
            char temp[64];
            snprintf(temp, 64, "%d. %s\n", i + 1, categories[i]);
            strcat(menu, temp);
        }
        send(clients[chooser].socket, menu, strlen(menu), 0);
    }

    while ((bytes_received = recv(client->socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received - 1] = '\0';
        
        if (!game_started) continue;

        if (strlen(current_category) == 0) {
            int choice = atoi(buffer);
            if (choice >= 1 && choice <= num_categories) {
                strcpy(current_category, categories[choice - 1]);
                char cat_msg[BUFFER_SIZE];
                snprintf(cat_msg, BUFFER_SIZE, "\nCategory chosen: %s\n", current_category);
                send_to_all(cat_msg);
                broadcast_question();
            }
        } else {
            int answer = atoi(buffer);
            if (answer >= 1 && answer <= 4) {
                check_answer(answer, client);
            }
        }
    }

    close(client->socket);
    return NULL;
}
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    srand(time(NULL));
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(atoi(argv[1]));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 2) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Quiz server started on port %s\nWaiting for players...\n", argv[1]);

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        
        int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t*)&client_len);
        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        if (client_count >= MAX_CLIENTS) {
            send(new_socket, "Game is full. Try again later.\n", 30, 0);
            close(new_socket);
            continue;
        }

        pthread_mutex_lock(&mutex);
        clients[client_count].socket = new_socket;
        clients[client_count].score = 0;
        clients[client_count].answered_questions = 0;
        
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, &clients[client_count]);
        pthread_detach(thread_id);
        
        client_count++;
        pthread_mutex_unlock(&mutex);
    }

    return 0;
}