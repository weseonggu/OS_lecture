#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <signal.h>
#include <time.h>

// 데이터 구조체 정의
struct card {// 카드 한장의 정보
	int value; // 카드 숫자 (1~13)
	char suit; // 카드 문양
};
struct gameInfo{ // manager와 player간 주고 받는 데이터 형식 
    struct card cards[54]; // player가 현재 소유한 카드 정보
    int num_cards; // player가 현재 소유한 카드 개수
    struct card open_card; // 현재 open card 정보
    int next_turn;
};
struct socket_msg{
        char text[1024]; // 텍스트 메세지
        int flag; // 동작 flag
};

// 전역변수 선언
struct card cards[108]; // 카드 정보 (parent 프로세스만 사용)
struct card open_card; // 현재 open card 정보 (parent 프로세스만 사용)
int top_card = 0; // 덱에서 가장 위에 있는 카드의 정보 (parent 프로세스만 사용)
int child_client_sock; // 시그널 사용을 위해 client 소켓 저장 (child 프로세스만 사용)
int now_card=54;

//게임 진행을 위해 필요한 함수 정의
struct socket_msg receive_sock(int sock){ // 소켓 수신 함수
    struct socket_msg msg;
    recv(sock, (struct socket_msg*)&msg, sizeof(msg),0);
    return msg;
}
void send_sock(int sock, struct socket_msg msg){ // 소켓 송신 함수
    send(sock, (struct socket_msg*)&msg, sizeof(msg), 0);
}
void make_cards(){ // 카드 정보 생성
    // make cards
    printf("Make cards");
	for (int i=0;i<13;i++) {
		cards[i].value=i%13+1;
		cards[i].suit='c';
	}
	for (int i=0;i<13;i++) {
		cards[i+13].value=i%13+1;
		cards[i+13].suit='d';
	}
	for (int i=0;i<13;i++) {
		cards[i+26].value=i%13+1;
		cards[i+26].suit='h';
	}
	for (int i=0;i<13;i++) {
		cards[i+39].value=i%13+1;
		cards[i+39].suit='s';
	}
    cards[52].value=66;
	cards[52].suit='j';
    cards[53].value=70;
	cards[53].suit='j';
    for (int i=0;i<54;i++) {
		printf("(%c,%d) ", cards[i].suit,cards[i].value);
	}
    printf("\n\n");
}
void shuffle(struct card *cards, int num){ // 카드 랜덤 셔플
    srand(time(NULL));
    struct card temp;
    int randInt;
    printf("Shuffling the cards\n");
    for (int i=0; i<num-1; i++){
        randInt = rand() % (num-i) + i;
        temp = cards[i];
        cards[i] = cards[randInt];
        cards[randInt] = temp;
    }
    for (int i=0;i<54;i++) {
		printf("(%c,%d) ", cards[i].suit,cards[i].value);
	}
    printf("\n");
}
void child_proc(int sock, int client_id, int msqid_p2c, int msqid_c2p); // 자식 프로세스의 동작, 길어서 뒤에서 정의

//Signal handling 함수 정의
void my_turn(int sig){
    struct socket_msg msg;
    msg.flag = 0;
    sprintf(msg.text, "********************\n*******Its your turn\n********************\n");
    send_sock(child_client_sock, msg); // 안내문구 전송
}
void win_sig(int sig){
    struct socket_msg msg;
    msg.flag = -99;
    sprintf(msg.text, "You are the winner!\n");
    send_sock(child_client_sock, msg); // 게임 결과 전송
    close(child_client_sock); // socket close
    exit(0);
}
void lose_sig(int sig){
    struct socket_msg msg;
    msg.flag = -99;
    sprintf(msg.text, "You are the loser.\n");
    send_sock(child_client_sock, msg); // 게임 결과 전송
    close(child_client_sock); // socket close
    exit(0);
}
void tie_sig(int sig){
    struct socket_msg msg;
    msg.flag = -99;
    sprintf(msg.text, "Its tie....\n");
    send_sock(child_client_sock, msg); // 게임 결과 전송
    close(child_client_sock); // socket close
    exit(0);
}

void main(){
    /*********************************************************
     1. 소켓 통신을 위한 셋팅 *********************************
    **********************************************************/
    int s_sock_fd, new_sock_fd;
    struct sockaddr_in s_address, c_address;
    int addrlen = sizeof(s_address);
    int check;
    int child_process_num = 0; // 서버에 접속한 클라이언트의 수
    
    s_sock_fd = socket(AF_INET, SOCK_STREAM, 0);// 서버 소켓 선언 (AF_INET: IPv4 인터넷 프로토콜, SOCK_STREAM: TCP/IP)
    if (s_sock_fd == -1){
        perror("socket failed: ");
        exit(1);
    }
        
    memset(&s_address, '0', addrlen); // s_address의 메모리 공간을 0으로 셋팅 / memset(메모리 공간 시작점, 채우고자 하는 값, 메모리 길이)
    s_address.sin_family = AF_INET; // IPv4 인터넷 프로토콜 사용을 명시
    s_address.sin_addr.s_addr = inet_addr("127.0.0.1"); // 서버 소켓의 주소 명시
    s_address.sin_port = htons(8080); // 서버 소켓의 포트번호 명시
    check = bind(s_sock_fd, (struct sockaddr *)&s_address, sizeof(s_address)); // 위에서 정의한 소켓 정보와 서버 소켓 Bind
    if(check == -1){
        perror("bind error: ");
        exit(1);
    }

    /*********************************************************
     2. IPC를 위한 셋팅 *************************************
    **********************************************************/
    pid_t pid_0, pid_1,pid_2,pid_3; // player 0과 palyer1의 pid를 저장할 변수
    key_t key_p2c_0 = 11111, key_c2p_0 = 22222;
    key_t key_p2c_1 = 33333, key_c2p_1 = 44444;

    key_t key_p2c_2 = 55555, key_c2p_2 = 66666;
    key_t key_p2c_3 = 77777, key_c2p_3 = 88888;
    int msqid_p2c_0, msqid_c2p_0, msqid_p2c_1, msqid_c2p_1, msqid_p2c_2, msqid_c2p_2, msqid_p2c_3, msqid_c2p_3;
    //Message queue 초기화 (clear up)
    if((msqid_p2c_0=msgget(key_p2c_0, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_p2c_0, IPC_RMID, NULL);
    if((msqid_c2p_0=msgget(key_c2p_0, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_c2p_0, IPC_RMID, NULL);
    if((msqid_p2c_1=msgget(key_p2c_1, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_p2c_1, IPC_RMID, NULL);
    if((msqid_c2p_1=msgget(key_c2p_1, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_c2p_1, IPC_RMID, NULL);

    if((msqid_p2c_2=msgget(key_p2c_2, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_p2c_2, IPC_RMID, NULL);
    if((msqid_c2p_2=msgget(key_c2p_2, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_c2p_2, IPC_RMID, NULL);
    if((msqid_p2c_3=msgget(key_p2c_3, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_p2c_3, IPC_RMID, NULL);
    if((msqid_c2p_3=msgget(key_c2p_3, IPC_CREAT|0666))==-1){exit(-1);}
    msgctl(msqid_c2p_3, IPC_RMID, NULL);
    
    // Message queue 생성
    if((msqid_p2c_0=msgget(key_p2c_0, IPC_CREAT|0666))==-1){exit(-1);}
    if((msqid_c2p_0=msgget(key_c2p_0, IPC_CREAT|0666))==-1){exit(-1);}
    if((msqid_p2c_1=msgget(key_p2c_1, IPC_CREAT|0666))==-1){exit(-1);}
    if((msqid_c2p_1=msgget(key_c2p_1, IPC_CREAT|0666))==-1){exit(-1);}

    if((msqid_p2c_2=msgget(key_p2c_2, IPC_CREAT|0666))==-1){exit(-1);}
    if((msqid_c2p_2=msgget(key_c2p_2, IPC_CREAT|0666))==-1){exit(-1);}
    if((msqid_p2c_3=msgget(key_p2c_3, IPC_CREAT|0666))==-1){exit(-1);}
    if((msqid_c2p_3=msgget(key_c2p_3, IPC_CREAT|0666))==-1){exit(-1);}

    struct gameInfo sending_info, receiving_info; // 데이터 주고 받기 위한 구조체 선언 (부모 프로세스가 사용)
    // 각 플레이어에게 전달할 초기 게임 정보 생성 및 전송
    printf("Set game info\n");
    make_cards(); // 카드 생성
    printf("%ld\n",sizeof(cards)/sizeof(struct card));
    shuffle(cards, 54); // 카드 셔플
    while(1){
        if(cards[24].suit=='j'){
            shuffle(cards,54);
        }
        else{
            break;
        }
    }
    printf("\n@@ cards setting---------------------------\n");
    top_card = 0; //  덱 가장 위에 있는 카드의 인덱스 표시

    //player 0에게 줄 첫 6장 카드 선택 및 전송
    sending_info.num_cards = 0;
    for (int i=0; i<6; i++){ 
        sending_info.cards[i]= cards[i];
        sending_info.num_cards ++;
        top_card += 1;
    }
    sending_info.open_card = cards[top_card+18];
    msgsnd(msqid_p2c_0, &sending_info, sizeof(struct gameInfo),0); // 첫 6장의 카드 전송
    //player 1에게 줄 첫 6장 카드 선택 및 전송
    sending_info.num_cards = 0;
    int i = 0;
    for (i=0; i < 6; i++){
        sending_info.cards[i]= cards[top_card];
        sending_info.num_cards ++;
        top_card += 1;
    }
    sending_info.open_card = cards[top_card+12];
    msgsnd(msqid_p2c_1, &sending_info, sizeof(struct gameInfo), 0); // 첫 6장의 카드 전송


    //player 2에게 줄 첫 6장 카드 선택 및 전송
    sending_info.num_cards = 0;
    for (i=0; i < 6; i++){
        sending_info.cards[i]= cards[top_card];
        sending_info.num_cards ++;
        top_card += 1;
    }
    sending_info.open_card = cards[top_card+6];
    msgsnd(msqid_p2c_2, &sending_info, sizeof(struct gameInfo), 0); // 첫 6장의 카드 전송
    
    
    
    //player 3에게 줄 첫 6장 카드 선택 및 전송
    sending_info.num_cards = 0;
    for (i=0; i < 6; i++){
        sending_info.cards[i]= cards[top_card];
        sending_info.num_cards ++;
        top_card += 1;
    }
    sending_info.open_card = cards[top_card];
    open_card = cards[top_card];
    top_card++;
    msgsnd(msqid_p2c_3, &sending_info, sizeof(struct gameInfo), 0); // 첫 6장의 카드 전송

    while(1){
    /*********************************************************
     4. 게임 플레이어 접속 대기 ********************************
    **********************************************************/
        // 클라이언트의 접속을 기다림
        check = listen(s_sock_fd, 4);
        if(check==-1){
            perror("listen failed: ");
            exit(1);
        }

        // 클라이언트의 접속을 허가함 -> 접속에 성공한 클라이언트와의 통신을 위해 새로운 소켓을 생성함
        new_sock_fd = accept(s_sock_fd, (struct sockaddr *) &c_address, (socklen_t*)&addrlen);
        if(new_sock_fd<0){
            perror("accept failed: ");
            exit(1);
        }

        //게임 진행을 위해 자식 프로세스 생성
        int c_pid = fork();
        if(c_pid==0){ // 자식 프로세스
            if(0==child_process_num){
                child_proc(new_sock_fd,  child_process_num, msqid_p2c_0, msqid_c2p_0); // player 0을 담당할 자식 프로세스
            }
            else if(1==child_process_num){
                child_proc(new_sock_fd,  child_process_num,  msqid_p2c_1, msqid_c2p_1); // player 1을 담당할 자식 프로세스
            }
            else if(2==child_process_num){
                child_proc(new_sock_fd,  child_process_num,  msqid_p2c_2, msqid_c2p_2); // player 2을 담당할 자식 프로세스
            }
            else if(3==child_process_num){
                child_proc(new_sock_fd,  child_process_num,  msqid_p2c_3, msqid_c2p_3); // player 3을 담당할 자식 프로세스
            }
            
        }
        else{ // 부모 프로세스
            if (child_process_num==0){
                pid_0 = c_pid; // 첫번째 자식 프로세스의 pid 저장
            }
            else if (child_process_num==1){
                pid_1 = c_pid; // 두번째 자식 프로세스의 pid 저장
            }
            else if (child_process_num==2){
                pid_2 = c_pid; // 3번째 자식 프로세스의 pid 저장
            }
            else if (child_process_num==3){
                pid_3 = c_pid; // 4번째 자식 프로세스의 pid 저장
            }
            child_process_num ++;
            close(new_sock_fd);// 부모 프로세스는 새로운 소켓을 유지할 필요 없음.
    /*********************************************************
     5. 게임 시작 ********************************************
    **********************************************************/
            int player0=1;
            int player1=1;
            int player2=1;
            int player3=1;
            int last_win=4;
            if (child_process_num >=4){// 2명의 Client가 접속하면 게임 시작
                while(1){//반복되는 게임 동작
                    int drop=15;
                    if(player0){
                        ////////////////////////////////////////////////////////////////////////
                        // Player 0의 차례//////////////////////////////////////////////////////
                        //////////////////////////////////////////////////////////////////////
                        kill(pid_0, SIGUSR1);
                        printf("Player 0's turn\n");
                        drop=15;
                        while(1){
                            // 1. 현재 오픈카드 정보 전송
                            sending_info.open_card = open_card;
                            msgsnd(msqid_p2c_0, &sending_info, sizeof(struct gameInfo),0);
                            // 2. 플레이어 0의 게임 정보 수신
                            msgrcv(msqid_c2p_0, &receiving_info, sizeof(struct gameInfo), 0 ,0);
                            //joker drop card add
                            if (open_card.suit=='j'){
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                for(int i=0 ; i<open_card.value-60;i++){
                                    if(top_card==now_card) break;
                                    sending_info.cards[receiving_info.num_cards+i] = cards[top_card];
                                    sending_info.num_cards= receiving_info.num_cards+i+1;
                                    top_card ++; 
                                }//joker card operate
                                msgsnd(msqid_p2c_0, &sending_info, sizeof(struct gameInfo),0);
                                open_card=cards[top_card];
                                top_card++;
                                if (sending_info.num_cards > 14){
                                    kill(pid_0, SIGQUIT); // 패배
                                    printf("Player 0 lose\n");
                                    player0=0;
                                    printf("Put the card on the sever:");
                                    int j=0;
                                    for(int i= now_card;i< sending_info.num_cards+now_card;i++){
                                        cards[i]=sending_info.cards[j];
                                        j ++;
                                        printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                    }
                                    printf("\n");
                                    now_card+=sending_info.num_cards;
                                    open_card=cards[top_card-1];
                                    last_win-=1;
                                    break;
                                }
                                break;
                            }
                            //turn end
                            printf("next_turn: %d\n",receiving_info.next_turn);
                            if(receiving_info.next_turn==99){
                                if(receiving_info.open_card.suit=='j'){
                                    printf("%c",receiving_info.open_card.suit);
                                    open_card = receiving_info.open_card;
                                    break;
                                }
                                break;
                            }
                            // 3. 플레이어 0의 게임 정보에 따른 행동
                            if ((receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit)&& drop==15){
                                //3-1. open 카드 정보가 같으면, 플레이어가 카드를 내려놓지 못한 것. --> 새로운 카드 전달.
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                sending_info.cards[receiving_info.num_cards] = cards[top_card];
                                sending_info.num_cards = receiving_info.num_cards+1;
                                top_card ++;
                                msgsnd(msqid_p2c_0, &sending_info, sizeof(struct gameInfo),0); // 첫 6장의 카드 전송
                                break;
                            }
                            else{
                                //3-2. open 카드 정보가 다르면, 플레이어가 카드를 내려놓은 것. --> 현재 open 카드 정보 업데이트
                                
                                if (receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit){
                                    break;
                                }
                                open_card = receiving_info.open_card;
                                drop-=1;
                            }
                        }
                        // 4. 게임 종료 판단
                        if (receiving_info.num_cards == 0|| last_win==1){
                            kill(pid_0, SIGINT); // 승리
                            kill(pid_1, SIGQUIT); // 패배
                            kill(pid_2, SIGQUIT); // 패배
                            kill(pid_3, SIGQUIT); // 패배
                            printf("Player 0 Win\n");
                        }
                        if (receiving_info.num_cards >= 14){
                            kill(pid_0, SIGQUIT); // 패배
                            player0=0;
                            printf("Player 0 lose\n");
                            printf("Put the card on the sever:");
                            int j=0;
                            for(int i= now_card;i< receiving_info.num_cards+now_card;i++){
                                cards[i]=receiving_info.cards[j];
                                j ++;
                                printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                }
                            
                            cards[receiving_info.num_cards+now_card]=cards[top_card-1];
                            printf("%d (%c, %d), ",receiving_info.num_cards+now_card,cards[receiving_info.num_cards+now_card].suit,cards[receiving_info.num_cards+now_card].value);
                            printf("\n");
                            now_card+=receiving_info.num_cards+1;
                            last_win-=1;
                        }
                        if (receiving_info.num_cards<14 && top_card >= now_card){
                            //카드 다씀, 무승부
                            kill(pid_0, SIGILL);
                            kill(pid_1, SIGILL); 
                            kill(pid_2, SIGILL); 
                            kill(pid_3, SIGILL); 
                        }
                    }


                    if(player1){
                        ////////////////////////////////////////////////////////////////////////
                        // Player 1의 차례//////////////////////////////////////////////////////
                        ///////////////////////////////////////////////////////////////////////
                        kill(pid_1, SIGUSR1);
                        printf("Player 1's turn\n");
                        drop=15;
                        while(1){
                            // 1. 현재 오픈카드 정보 전송
                            sending_info.open_card = open_card;
                            msgsnd(msqid_p2c_1, &sending_info, sizeof(struct gameInfo),0);
                            // 2. 플레이어 1의 게임 정보 수신
                            msgrcv(msqid_c2p_1, &receiving_info, sizeof(struct gameInfo), 0 ,0);
                            //joker drop card add
                            if (open_card.suit=='j'){
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                for(int i=0 ; i<open_card.value-60;i++){
                                    if(top_card==now_card) break;
                                    sending_info.cards[receiving_info.num_cards+i] = cards[top_card];
                                    sending_info.num_cards= receiving_info.num_cards+i+1;
                                    top_card ++; 
                                }//joker card operate
                                msgsnd(msqid_p2c_1, &sending_info, sizeof(struct gameInfo),0);
                                open_card=cards[top_card];
                                top_card++;
                                if (sending_info.num_cards > 14){
                                    kill(pid_1, SIGQUIT); // 패배
                                    player1=0;
                                    printf("Player 1 lose\n");
                                    printf("Put the card on the sever:");
                                    int j=0;
                                    for(int i= now_card;i< sending_info.num_cards+now_card;i++){
                                        cards[i]=sending_info.cards[j];
                                        j ++;
                                        printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                    }
                                    printf("\n");
                                    now_card+=sending_info.num_cards;
                                    open_card=cards[top_card-1];
                                    last_win-=1;
                                    break;
                                }
                                break;
                            }
                            //turn end
                            printf("next_turn: %d\n",receiving_info.next_turn);
                            if(receiving_info.next_turn==99){
                                if(receiving_info.open_card.suit=='j'){
                                    open_card = receiving_info.open_card;
                                    break;
                                }
                                break;
                            }
                            // 3. 플레이어 1의 게임 정보에 따른 행동
                            if ((receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit)&& drop==15){
                                //3-1. open 카드 정보가 같으면, 플레이어가 카드를 내려놓지 못한 것. --> 새로운 카드 전달.
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                sending_info.cards[receiving_info.num_cards] = cards[top_card];
                                sending_info.num_cards = receiving_info.num_cards+1;
                                top_card ++;
                                msgsnd(msqid_p2c_1, &sending_info, sizeof(struct gameInfo),0);
                                break;
                            }
                            else{
                                //3-2. open 카드 정보가 다르면, 플레이어가 카드를 내려놓은 것. --> 현재 open 카드 정보 업데이트
                                if ((receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit)){
                                    break;
                                }
                                open_card = receiving_info.open_card;
                                drop-=1;
                            }
                        }
                        // 4. 게임 종료 판단
                        if (receiving_info.num_cards == 0|| last_win==1){
                            kill(pid_1, SIGINT); // 승리
                            kill(pid_0, SIGQUIT); // 패배
                            kill(pid_2, SIGQUIT); // 패배
                            kill(pid_3, SIGQUIT); // 패배
                            printf("Player 1 Win\n");
                        }
                        if (receiving_info.num_cards >= 14){
                            kill(pid_1, SIGQUIT); // 패배
                            player1=0;
                            printf("Player 1 lose\n");
                            printf("Put the card on the sever:");
                            int j=0;
                            for(int i= now_card;i< receiving_info.num_cards+now_card;i++){
                                cards[i]=receiving_info.cards[j];
                                j ++;
                                printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                }
                            cards[receiving_info.num_cards+now_card]=cards[top_card-1];
                            printf("%d (%c, %d), ",receiving_info.num_cards+now_card,cards[receiving_info.num_cards+now_card].suit,cards[receiving_info.num_cards+now_card].value);
                            printf("\n");
                            now_card+=receiving_info.num_cards+1;
                            last_win-=1;
                        }
                        if (receiving_info.num_cards<14 && top_card >= now_card){
                            //카드 다씀, 무승부
                            kill(pid_0, SIGILL);
                            kill(pid_1, SIGILL); 
                            kill(pid_2, SIGILL);
                            kill(pid_3, SIGILL);
                        }
                    }






                    if(player2){
                        ////////////////////////////////////////////////////////////////////////
                        // Player 2의 차례//////////////////////////////////////////////////////
                        ///////////////////////////////////////////////////////////////////////
                        kill(pid_2, SIGUSR1);
                        printf("Player 2's turn\n");
                        drop=15;
                        while(1){
                            // 1. 현재 오픈카드 정보 전송
                            sending_info.open_card = open_card;
                            msgsnd(msqid_p2c_2, &sending_info, sizeof(struct gameInfo),0);
                            // 2. 플레이어 2의 게임 정보 수신
                            msgrcv(msqid_c2p_2, &receiving_info, sizeof(struct gameInfo), 0 ,0);
                            //joker drop card add
                            if (open_card.suit=='j'){
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                for(int i=0 ; i<open_card.value-60;i++){
                                    if(top_card==now_card) break;
                                    sending_info.cards[receiving_info.num_cards+i] = cards[top_card];
                                    sending_info.num_cards= receiving_info.num_cards+i+1;
                                    top_card ++; 
                                }//joker card operate
                                msgsnd(msqid_p2c_2, &sending_info, sizeof(struct gameInfo),0);
                                open_card=cards[top_card];
                                top_card++;
                                if (sending_info.num_cards > 14){
                                    kill(pid_2, SIGQUIT); // 패배
                                    player2=0;
                                    printf("Player 2 lose\n");
                                    printf("Put the card on the sever:");
                                    int j=0;
                                    for(int i= now_card;i< sending_info.num_cards+now_card;i++){
                                        cards[i]=sending_info.cards[j];
                                        j ++;
                                        printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                    }
                                    printf("\n");
                                    now_card+=sending_info.num_cards;
                                    open_card=cards[top_card-1];
                                    last_win-=1;
                                    break;
                                }
                                break;
                            }
                            //turn end
                            printf("next_turn: %d\n",receiving_info.next_turn);
                            if(receiving_info.next_turn==99){
                                if(receiving_info.open_card.suit=='j'){
                                    open_card = receiving_info.open_card;
                                    break;
                                }
                                break;
                            }
                            // 3. 플레이어 1의 게임 정보에 따른 행동
                            if ((receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit)&& drop==15){
                                //3-1. open 카드 정보가 같으면, 플레이어가 카드를 내려놓지 못한 것. --> 새로운 카드 전달.
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                sending_info.cards[receiving_info.num_cards] = cards[top_card];
                                sending_info.num_cards = receiving_info.num_cards+1;
                                top_card ++;
                                msgsnd(msqid_p2c_2, &sending_info, sizeof(struct gameInfo),0);
                                break;
                            }
                            else{
                                //3-2. open 카드 정보가 다르면, 플레이어가 카드를 내려놓은 것. --> 현재 open 카드 정보 업데이트
                                if ((receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit)){
                                    break;
                                }
                                open_card = receiving_info.open_card;
                                drop-=1;
                            }
                        }
                        // 4. 게임 종료 판단
                        if (receiving_info.num_cards == 0|| last_win==1){
                            kill(pid_2, SIGINT); // 승리
                            kill(pid_0, SIGQUIT); // 패배
                            kill(pid_1, SIGQUIT); // 패배
                            kill(pid_3, SIGQUIT); // 패배
                            printf("Player 2 Win\n");
                        }
                        if (receiving_info.num_cards >= 14){
                            kill(pid_2, SIGQUIT); // 패배
                            player2=0;
                            printf("Player 2 lose\n");
                             printf("Put the card on the sever:");
                             printf("Put the card on the sever:");
                            int j=0;
                            for(int i= now_card;i< receiving_info.num_cards+now_card;i++){
                                cards[i]=receiving_info.cards[j];
                                j ++;
                                printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                }
                            cards[receiving_info.num_cards+now_card]=cards[top_card-1];
                            printf("%d (%c, %d), ",receiving_info.num_cards+now_card,cards[receiving_info.num_cards+now_card].suit,cards[receiving_info.num_cards+now_card].value);
                            printf("\n");
                            now_card+=receiving_info.num_cards+1;
                            last_win-=1;
                        }
                        if (receiving_info.num_cards<14 && top_card >= now_card){
                            //카드 다씀, 무승부
                            kill(pid_0, SIGILL);
                            kill(pid_1, SIGILL); 
                            kill(pid_2, SIGILL);
                            kill(pid_3, SIGILL);
                        }
                    }






                    if(player3){
                        ////////////////////////////////////////////////////////////////////////
                        // Player 3의 차례//////////////////////////////////////////////////////
                        ///////////////////////////////////////////////////////////////////////
                        kill(pid_3, SIGUSR1);
                        printf("Player 3's turn\n");
                        drop=15;
                        while(1){
                            // 1. 현재 오픈카드 정보 전송
                            sending_info.open_card = open_card;
                            msgsnd(msqid_p2c_3, &sending_info, sizeof(struct gameInfo),0);
                            // 2. 플레이어 1의 게임 정보 수신
                            msgrcv(msqid_c2p_3, &receiving_info, sizeof(struct gameInfo), 0 ,0);
                            //joker drop card add
                            if (open_card.suit=='j'){
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                for(int i=0 ; i<open_card.value-60;i++){
                                    if(top_card==now_card) break;
                                    sending_info.cards[receiving_info.num_cards+i] = cards[top_card];
                                    sending_info.num_cards= receiving_info.num_cards+i+1;
                                    top_card ++; 
                                }//joker card operate
                                msgsnd(msqid_p2c_3, &sending_info, sizeof(struct gameInfo),0);
                                open_card=cards[top_card];
                                top_card++;
                                if (sending_info.num_cards > 14){
                                    kill(pid_3, SIGQUIT); // 패배
                                    player3=0;
                                    printf("Player 3 lose\n");
                                    printf("Put the card on the sever:");
                                    int j=0;
                                    for(int i= now_card;i< sending_info.num_cards+now_card;i++){
                                        cards[i]=sending_info.cards[j];
                                        j ++;
                                        printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                    }
                                    printf("\n");
                                    now_card+=sending_info.num_cards;
                                    open_card=cards[top_card-1];
                                    last_win-=1;
                                    break;
                                }
                                break;
                            }
                            //turn end
                            printf("next_turn: %d\n",receiving_info.next_turn);
                            if(receiving_info.next_turn==99){
                                if(receiving_info.open_card.suit=='j'){
                                    open_card = receiving_info.open_card;
                                    break;
                                }
                                break;
                            }
                            // 3. 플레이어 1의 게임 정보에 따른 행동
                            if ((receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit)&& drop==15){
                                //3-1. open 카드 정보가 같으면, 플레이어가 카드를 내려놓지 못한 것. --> 새로운 카드 전달.
                                for(int i=0; i<receiving_info.num_cards; i++){
                                    sending_info.cards[i] = receiving_info.cards[i];
                                }
                                sending_info.cards[receiving_info.num_cards] = cards[top_card];
                                sending_info.num_cards = receiving_info.num_cards+1;
                                top_card ++;
                                msgsnd(msqid_p2c_3, &sending_info, sizeof(struct gameInfo),0);
                                break;
                            }
                            else{
                                //3-2. open 카드 정보가 다르면, 플레이어가 카드를 내려놓은 것. --> 현재 open 카드 정보 업데이트
                                if ((receiving_info.open_card.value == open_card.value && receiving_info.open_card.suit == open_card.suit)){
                                    break;
                                }
                                open_card = receiving_info.open_card;
                                drop-=1;
                            }
                        }
                        // 4. 게임 종료 판단
                        if (receiving_info.num_cards == 0 || last_win==1){
                            kill(pid_3, SIGINT); // 승리
                            kill(pid_0, SIGQUIT); // 패배
                            kill(pid_2, SIGQUIT); // 패배
                            kill(pid_1, SIGQUIT); // 패배
                            printf("Player 3 Win\n");
                        }
                        if (receiving_info.num_cards >= 14){
                            kill(pid_3, SIGQUIT); // 패배
                            player3=0;
                            printf("Player 3 lose\n");
                            printf("Put the card on the sever:");
                            int j=0;
                            for(int i= now_card;i< receiving_info.num_cards+now_card;i++){
                                cards[i]=receiving_info.cards[j];
                                j ++;
                                printf("%d (%c, %d), ",i,cards[i].suit,cards[i].value);
                                }
                            cards[receiving_info.num_cards+now_card]=cards[top_card-1];
                            printf("%d (%c, %d), ",receiving_info.num_cards+now_card,cards[receiving_info.num_cards+now_card].suit,cards[receiving_info.num_cards+now_card].value);
                            printf("\n");
                            now_card+=receiving_info.num_cards+1;
                            last_win-=1;
                        }
                        if (receiving_info.num_cards<14 && top_card >= now_card){
                            //카드 다씀, 무승부
                            kill(pid_0, SIGILL);
                            kill(pid_1, SIGILL); 
                            kill(pid_2, SIGILL);
                            kill(pid_3, SIGILL);
                        }
                    }
                }
                exit(0); // 게임이 끝나면 프로세스 종료
            }
        }
    }
}

void child_proc(int sock, int client_id, int msqid_p2c, int msqid_c2p){
    /* 매개변수
        0. 소켓fd
        1. client 번호 (혹은 player 번호)
        2. parent to child 메세지큐 id
        3. child to parent 메세지큐 id
    */
    printf("child(%d) joined.\n", client_id);
    struct socket_msg msg; // client와 정보를 주고 받을 구조체
    struct gameInfo player_info; // player의 게임정보
    struct gameInfo receiving_info; // 부모 프로세스로에게 전달받을 게임 정보
    child_client_sock = sock; // 시그널 사용을 위해 sock 전역변수에 저장

    signal(SIGINT, win_sig); // 승리 시그널
    signal(SIGQUIT, lose_sig); //패배 시그널
    signal(SIGILL, tie_sig); //무승부 시그널

    // 초기 정보 수신
    msgrcv(msqid_p2c, &player_info, sizeof(struct gameInfo), 0 , 0);
    // msg.flag=0 -> 안내 문구
    // msg.flag=1 -> 안내문구 + 입력요망
    msg.flag = 0;
    sprintf(msg.text,"You got six cards.\n");
    send(sock, (struct socket_msg*)&msg, sizeof(msg), 0);
    for (int i=0; i<player_info.num_cards; i++) {
        sprintf(msg.text, "%d:(%c,%d), ", i, player_info.cards[i].suit, player_info.cards[i].value);
        send_sock(sock, msg);
    }
    sprintf(msg.text, "\nGame will starts soon----------------.\n");
    send_sock(sock, msg);
    
    while(1){
        // 시그널 올 때까지 대기
        signal(SIGUSR1, my_turn);
        pause();
        player_info.next_turn=0;
        int drop_card_num=15;
        while(1){
            // 부모 프로세스로부터 정보 수신 및 업데이트
            msgrcv(msqid_p2c, &receiving_info, sizeof(struct gameInfo), 0 ,0);
            player_info.open_card = receiving_info.open_card;
            printf("Current Open Card: (%c,%d)\n", player_info.open_card.suit, player_info.open_card.value);


            // 현재 오픈 카드 정보 Client에게 송신
            sprintf(msg.text, "--------------------------------------------------\n");
            send_sock(sock, msg);
            msg.flag = 0;
            sprintf(msg.text, "Current Open Card: (%c,%d)\n", player_info.open_card.suit, player_info.open_card.value);
            send_sock(sock, msg);

            //drop joker
            if(player_info.open_card.suit=='j'){
                player_info.open_card=player_info.open_card;
                msgsnd(msqid_c2p, &player_info, sizeof(struct gameInfo),0);


                msgrcv(msqid_p2c, &receiving_info, sizeof(struct gameInfo), 0 ,0);
                player_info.num_cards = receiving_info.num_cards;
                sprintf(msg.text, "Your Card: ");
                send_sock(sock, msg); // 업데이트 된 정보 전송
                for (int i=0; i < player_info.num_cards; i++){ // 카드정보 갱신
                    player_info.cards[i].value = receiving_info.cards[i].value;
                    player_info.cards[i].suit = receiving_info.cards[i].suit;
                    sprintf(msg.text, "(%c,%d), ", player_info.cards[i].suit, player_info.cards[i].value);
                    send_sock(sock, msg); // 업데이트 된 정보 전송
                }
                sprintf(msg.text, "\n");
                send_sock(sock, msg); // 업데이트 된 정보 전송
                break;
                
            }





        
            // 현재 내 카드 정보 Client에게 송신
            sprintf(msg.text, "Your Cards List\n");
            send_sock(sock, msg);
            for (int i=0; i<player_info.num_cards; i++) {
                sprintf(msg.text, "%d:(%c,%d), ", i, player_info.cards[i].suit, player_info.cards[i].value);
                send_sock(sock, msg);
            }
            sprintf(msg.text, "\n");
            send_sock(sock, msg);
            sprintf(msg.text, "--------------------------------------------------\n");
            send_sock(sock, msg);

            // 드롭할 카드 선택 요청 (client로 부터 입력받아야 함)
            int user_input;
            msg.flag = 1; // input flag
            if(drop_card_num==15){
                sprintf(msg.text, "Select card index(Do not press 99):");
            }
            else{
                sprintf(msg.text,"Select card index(Press 99 to exit):");
            }
            
            send_sock(sock, msg);

            // *client의 입력결과 수신
            // *수신된 정보 부모 프로세스에게 전달
            // *부모 프로세스에게 게임진행 결과 전달받아서, player info 수정
            msg = receive_sock(sock); // 클라이언트 입력결과 수신
            user_input = msg.flag;
            //user drop cards comfirm
            if(user_input != 99){
                if(player_info.num_cards-1 < user_input && drop_card_num != 15){
                    msg.flag=0;
                    sprintf(msg.text,"input is over. \n");
                    send_sock(sock, msg);
                    player_info.next_turn=99;
                    msgsnd(msqid_c2p, &player_info, sizeof(struct gameInfo),0);
                    break;
                }
                else if(((player_info.num_cards > user_input && player_info.cards[user_input].suit != player_info.open_card.suit && player_info.cards[user_input].value != player_info.open_card.value) && player_info.cards[user_input].suit !='j' ) && drop_card_num !=15){
                    msg.flag=0;
                    sprintf(msg.text,"input is wrong. \n");
                    send_sock(sock, msg);
                    player_info.next_turn=99;
                    msgsnd(msqid_c2p, &player_info, sizeof(struct gameInfo),0);
                    break;
                }
            }

            // proper card drop
            if (((player_info.cards[user_input].suit==player_info.open_card.suit || player_info.cards[user_input].value==player_info.open_card.value) && !(drop_card_num==15 && user_input==99)) || player_info.cards[user_input].suit=='j'      /*or joker*/     ){
                //카드 드롭

                //joker cards drop
                if (player_info.cards[user_input].suit=='j'){// joker
                    player_info.next_turn=99;
                    msg.flag=0;
                   
                    if(player_info.cards[user_input].value==66){
                        sprintf(msg.text, "Gray joker is dropped\n");
                    }
                    else{
                        sprintf(msg.text, "Color joker is dropped\n");
                    }
                    send_sock(sock, msg);
                    struct card temp = player_info.cards[user_input];
                    for (int i=user_input; i < player_info.num_cards; i++){ // 카드 리스트에서 드롭할 카드 빼기
                        player_info.cards[i].value = player_info.cards[i+1].value;
                        player_info.cards[i].suit = player_info.cards[i+1].suit;
                    }
                    player_info.num_cards -= 1;
                    player_info.open_card = temp;

                    msgsnd(msqid_c2p, &player_info, sizeof(struct gameInfo),0);
                    break;
                }
                //else card drop
                if (player_info.num_cards == 1){//normal
                    // user_input 0, num cards가 1일때 오류 발생하는거 방지. (my_info.cards 정리하며 오류 발생.)
                    player_info.num_cards=0;
                }
                else{

                    struct card temp = player_info.cards[user_input];
                    for (int i=user_input; i < player_info.num_cards; i++){ // 카드 리스트에서 드롭할 카드 빼기
                        player_info.cards[i].value = player_info.cards[i+1].value;
                        player_info.cards[i].suit = player_info.cards[i+1].suit;
                    }
                    player_info.num_cards -= 1;
                    player_info.open_card = temp;
                    msg.flag = 0;
                    sprintf(msg.text, "Card (%c,%d) is dropped\n", temp.suit, temp.value);
                    send_sock(sock, msg); // 업데이트 된 정보 전송
                }

                 msgsnd(msqid_c2p, &player_info, sizeof(struct gameInfo),0);
            }
            else{
                //카드 드롭 X
                msg.flag = 0;
                if(user_input==99 && drop_card_num != 15){
                    sprintf(msg.text, "99 pressed.\n");
                    player_info.next_turn=99;
                }
                else{
                    sprintf(msg.text, "You cannot drop this card. You will have another card.\n");
                }   
                send_sock(sock, msg); // 업데이트 된 정보 전송
                // 업데이트 된 정보 전송
                msgsnd(msqid_c2p, &player_info, sizeof(struct gameInfo),0);
                if(user_input==99 && drop_card_num != 15){
                    break;
                }
                // 새로운 카드가 포함된 정보 수신.
                msgrcv(msqid_p2c, &receiving_info, sizeof(struct gameInfo), 0 ,0);
                player_info.num_cards = receiving_info.num_cards;
                for (int i=0; i < player_info.num_cards; i++){ // 카드정보 갱신
                    player_info.cards[i].value = receiving_info.cards[i].value;
                    player_info.cards[i].suit = receiving_info.cards[i].suit;
                    if(i==player_info.num_cards-1){
                        sprintf(msg.text, "Card (%c,%d) is added\n", player_info.cards[i].suit, player_info.cards[i].value);
                        send_sock(sock, msg); // 업데이트 된 정보 전송
                    }
                }
                break;
            }
            drop_card_num-=1;
        }
        msg.flag = 0;
        sprintf(msg.text, "Your turn is over. Waiting for the next turn.\n--------------------------------------------------\n");
        send_sock(sock, msg); // 턴 종료 후 대기
    }
}