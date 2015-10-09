/*
 * File:   main.cpp
 * Author: emi
 *
 * Created on 19 de septiembre de 2015, 10:27 AM
 */

#include <stdio.h>
#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <string.h>
#include <time.h>
#include <unistd.h> //sleep

using namespace std;

#include "rdtPrueba.h"
//#include "rdt.h"
#include "constantes.h"

//FIXME esto es para probar tiene que volar
//#include "rdt_test.h"

#define RELAYED_MESSAGE "RELAYED_MESSAGE"
#define PRIVATE_MESSAGE "PRIVATE_MESSAGE"
#define CONNECTED "CONNECTED"
#define GOODBYE "GOODBYE"

#define TTL_CLIENTES 15
#define MONITOR_TIME 30

int socketMulticast;

typedef struct Cliente {
        char nick[50];
        int puerto;
        char ip[20];
        int cantMensajes;//Es neecesario?
        time_t ult_actividad;
} Cliente;

typedef struct Mensaje {
        char  destino[MAX_IP_LENGTH];
        int   dest_puerto;
        char  origen[MAX_IP_LENGTH];
        int   orig_puerto;
        char  msg[MAX_TEXTO];
        bool  multicast;

} Mensaje;
typedef map<string, Cliente*> MapClientes;

typedef enum  {COM_LOGIN, COM_LOGOUT, COM_GET_CONNECTED, COM_MSG, COM_PVT_MSG, COM_INVALID} MsgComand;

MapClientes* Clientes = new MapClientes;


time_t start = time(0);

typedef queue<Mensaje*> ColaMensajes;

ColaMensajes* colaMensajes = new ColaMensajes;

char* ipServidor = IP_SERVIDOR;
int puertoServidor;

char* ipMulticast = IP_MULTICAST;
int puertoMulticast = PUERTO_MULTICAST;

pthread_mutex_t queueMutex;
pthread_cond_t emitCond;

pthread_mutex_t clientesMutex;

int socEmisor = 0;
int socReceptor = 0;

int cantMensajes = 0;
int cantConexiones = 0;


void loginCliente(Cliente* c) {
        Clientes->insert(make_pair(c->ip, c));
}
void logOut(Cliente* c){
        Clientes->erase(c->ip);
};

Cliente* getCliente(char* ip){
        try {
                return Clientes->at(ip);
        }catch(...) {
                cout <<
                "cliente no existe" << endl;
                return NULL;
        }
}

TablaClienteId* getClientesIdForMulticast(){
  TablaClienteId* tabla = new TablaClienteId;
  MapClientes::iterator it = Clientes->begin();
  char auxStr[MSGBUFSIZE];
  while(it != Clientes->end()) {
    //lleno la tabla con ["ip:puerto"]=flase
    sprintf(auxStr, "%s:%d", it->second->ip, it->second->puerto);
    tabla->insert(make_pair(auxStr, false));
    ++it;
  }
  return tabla;
}

//Se tiene que llamar con el mutex de clientes ya pedido
Cliente* getClienteByNick(const char* nick) {
        Cliente * ret = NULL;
        map<string, Cliente*>::iterator iter = Clientes->begin();
        while (iter != Clientes->end()) {
                ret = iter->second;
                if (strcmp(ret->nick, nick) == 0) {
                        return ret;
                }
                ++iter;
        }

        return ret;
}



Mensaje* crearMensaje(char* ipDestino,int puerto, bool multicast, char* contenido) {

        Mensaje* ret = new Mensaje();
        strcpy(ret->origen,IP_SERVIDOR);
        strcpy(ret->msg, contenido);
        strcpy(ret->destino, ipDestino);

        ret->dest_puerto = puerto;
        ret->multicast = multicast;

        return ret;
}

void encolarMensaje(Mensaje* mensaje) {
        pthread_mutex_lock(&queueMutex);
        colaMensajes->push(mensaje);
        pthread_cond_signal(&emitCond);
        pthread_mutex_unlock(&queueMutex);
}

//Hay que llamar a esta con el mutex de clientes bloqueado.
char* getConected(){
        char* retStr = new char[MAX_LARGO_MENSAJE];
        MapClientes::iterator it = Clientes->begin();
        if (it != Clientes->end()) {
                if(Clientes->size()==1){
                  strcat(retStr, it->second->nick);
                }
                else{
                  strcat(retStr, it->second->nick);
                  ++it;
                  while(it != Clientes->end()) {
                          strcat(retStr, "|");
                          strcat(retStr, it->second->nick);
                          ++it;
                  }
                }
        }
        else {
                sprintf(retStr, "<No hay usuarios conectados>");
        }
        return retStr;
}

int processGetConnectedMsg(char* ip, int puerto) {
        char contenido[MAX_TEXTO];
        pthread_mutex_lock(&clientesMutex);
        if (Clientes->find(ip) != Clientes->end()) {
                Cliente * cli = Clientes->at(ip);
                cli->ult_actividad = time(0);
                sprintf(contenido, "%s %s", CONNECTED, getConected());
                pthread_mutex_unlock(&clientesMutex);
                Mensaje* mensaje = crearMensaje(ip, puerto, false, contenido);
                encolarMensaje(mensaje);
                return 0;
        }
        pthread_mutex_unlock(&clientesMutex);
        return -1;
}

int processLoginMsg(char* ip, int puerto, char * msg) {

        pthread_mutex_lock(&clientesMutex);
        if (Clientes->find(ip) == Clientes->end()) {
                Cliente * cli = new Cliente();
                string nick = msg;
                nick = nick.substr(nick.find(" ") + 1);
                strcpy(cli->nick, nick.c_str());
                strcpy(cli->ip, ip);
                cli->cantMensajes = 0;
                cli->puerto = puerto;
                cli->ult_actividad = time(0);
                Clientes->insert(make_pair(cli->ip, cli));
                pthread_mutex_unlock(&clientesMutex);
                cantConexiones++;
                return 0;
        }
        pthread_mutex_unlock(&clientesMutex);
        return -1;
}

int processLogut(char* ip, int puerto) {
        pthread_mutex_lock(&clientesMutex);
        Clientes->erase(ip);
        pthread_mutex_unlock(&clientesMutex);
        //TODO Ver si se manda un goodbye o no cuando se hace un logout
        //char contenido[MAX_TEXTO] = GOODBYE;
        //Mensaje* mensaje = crearMensaje(ip, puerto, false, contenido);
        //encolarMensaje(mensaje);
        return 0;
}

int processMulticastMessage(char* sourceIp, char* recv_msg) {
        pthread_mutex_lock(&clientesMutex);
        map<string, Cliente*>::iterator iter = Clientes->find(sourceIp);

        if (iter != Clientes->end()) {
                Cliente* cli = iter->second;
                pthread_mutex_unlock(&clientesMutex);
                cli->ult_actividad = time(0);
                string str_contenido = recv_msg;
                str_contenido = str_contenido.substr(str_contenido.find(" ") +1);

                char contenido[MAX_TEXTO];
                sprintf(contenido, "%s %s %s", RELAYED_MESSAGE, cli->nick, str_contenido.c_str());

                Mensaje* mensaje = crearMensaje(ipMulticast, puertoMulticast, true, contenido);
                encolarMensaje(mensaje);
                return 0;

        }
        pthread_mutex_unlock(&clientesMutex);
        return -1;
}

int processPrivatetMessage(char* sourceIp, char* recv_msg) {

        pthread_mutex_lock(&clientesMutex);
        map<string, Cliente*>::iterator iter = Clientes->find(sourceIp);

        if (iter != Clientes->end()) {
                Cliente* cli = iter->second;
                cli->ult_actividad = time(0);
                string str_recv_msg = recv_msg;

                //Descarto el cabezal private msg
                str_recv_msg = str_recv_msg.substr(str_recv_msg.find(" ") +1);

                //Nick esta desde el inicio hasta el primer espacio
                const char* dest_nick = str_recv_msg.substr(0, str_recv_msg.find(" ")).c_str();

                Cliente* dest_cli = getClienteByNick(dest_nick);
                pthread_mutex_unlock(&clientesMutex);
                if (dest_cli != NULL) {
                        //Descarto el nick y me quedo con el mensaje
                        str_recv_msg = str_recv_msg.substr(str_recv_msg.find(" ") +1);

                        char contenido[MAX_TEXTO];
                        sprintf(contenido, "%s %s %s", PRIVATE_MESSAGE, cli->nick, str_recv_msg.c_str());

                        Mensaje* mensaje = crearMensaje(dest_cli->ip, dest_cli->puerto, false, contenido);
                        encolarMensaje(mensaje);

                        return 0;
                }
        }
        pthread_mutex_unlock(&clientesMutex);
        return -1;
}



void parseMessage(Cliente* c, char* mensaje){
        string comando = mensaje;

        // if (comando.find(LOGIN) == 0) {
        //         //obtengo nombre de usuario
        //         strcpy(c->nick,mensaje);
        //         loginCliente(c);
        // } else
        if (comando.find(LOGOUT) == 0) {
                //desloegueo al usuario
                logOut(c);
        } else if (comando.find(GET_CONNECTED) == 0) {
                //envio conectados
                //char* conectados = getConected();
        } else if (comando.find(MESSAGE) == 0) {
                //envio mensaje multicast
        } else if (comando.find(PRIVATE_MESSAGE) == 0) {
                //envio mensaje privado
        } else {
                std::cout << "Ha llegado un mensaje invallido hacia el servidor." << std::endl;
        }
}

MsgComand getCommandFromMsg(char* msg) {
        string comando = msg;
        MsgComand ret = COM_INVALID;

        if (comando.find(LOGIN) == 0) {
                ret = COM_LOGIN;
        } else if (comando.find(LOGOUT) == 0) {
                ret = COM_LOGOUT;
        } else if (comando.find(GET_CONNECTED) == 0) {
                ret = COM_GET_CONNECTED;
        } else if (comando.find(MESSAGE) == 0) {
                ret = COM_MSG;
        } else if (comando.find(PRIVATE_MESSAGE) == 0) {
                ret = COM_PVT_MSG;
        }

        return ret;
}



void* debug(){
        cout << "::DEBUG:: escribe ip de cliente" << endl;
        string ip;
        getline(cin, ip);
        cout << "::DEBUG:: escribe un mensaje que llega desde cliente" << endl;
        cout << ">";
        string comando;
        getline(cin, comando);

        char * cstrComando = new char [comando.length()+1];
        strcpy (cstrComando, comando.c_str());

        char * cstrIp = new char [comando.length()+1];
        strcpy (cstrIp, comando.c_str());

        Cliente* c = getCliente(cstrIp);


        parseMessage(c, cstrComando);
        return NULL;
};

void* debugRdt(){
        cout << ">";
        string comando;
        getline(cin, comando);
        char* mensaje = new char[MAX_LARGO_MENSAJE];
        strcpy(mensaje, comando.c_str());
        char* mensajeToSend = new char[50];
        strcpy(mensajeToSend, "MESSAGE Debug multicast");
        TablaClienteId* tablaClientes = getClientesIdForMulticast();

        cout << "DEBUG:::enviado" << endl;
        rdt_send_multicast(socketMulticast, mensajeToSend , tablaClientes);
        //sendMulticast(mensaje);
        return NULL;
};

void clientesConectados(){
        pthread_mutex_lock(&clientesMutex);
        char * conectados =  getConected();
        pthread_mutex_unlock(&clientesMutex);
        cout << conectados << endl;
}
void mensajesEnviados(){
        cout << "Mensajes enviados: " << cantMensajes << endl;
}
void conexionesTotales(){

        cout << "Cantidad de conexiones totales: "<< cantConexiones << endl;
}
void tiempoEjecucion(){

        double seconds_since_start;
        seconds_since_start = difftime( time(0), start);
        std::cout << "Han pasado "  << seconds_since_start << " segundos" << std::endl;
}

int consola() {
        char c;

        do {
          cout << ">" ;
                string comando;
                getline(cin, comando);
                c=comando.c_str()[0];
                if (c=='a') {
                        // * a – cantidad de clientes conectados -- clientes logueados
                        clientesConectados();
                        /* code */
                } else if (c=='s') {
                        // * s – cantidad de mensajes enviados -- lista de mensajes
                        mensajesEnviados();
                        /* code */
                } else if (c=='d') {
                        // * d – cantidad de conexiones totales
                        conexionesTotales();
                        /* code */
                } else if (c=='f') {
                        // * f – tiempo (wall time) de ejecución
                        tiempoEjecucion();
                } else if (c=='x') {
                        // * f – tiempo (wall time) de ejecución
                        cout << "DEBUG::" ;
                        debugRdt();
                } else {
                        cout << "Comando no reconocido." << endl;
                }
        } while (c != 'e');
        return 0;
}

void* emisorMensajes(void*) {

        while (true) {
                //mutex_lock
                pthread_mutex_lock(&queueMutex);
                if (colaMensajes->empty()) {
                        //wait_cond
                        pthread_cond_wait(&emitCond,&queueMutex);
                        //cout << "Emisor liberado" << endl;
                }

                Mensaje * msg = colaMensajes->front();

                colaMensajes->pop();
                //mutex_unlock
                pthread_mutex_unlock(&queueMutex);

                /*appMsg* rdt_msg = new appMsg();
                strcpy(rdt_msg->mensaje, msg->msg);
                strcpy(rdt_msg->source_ip, msg->origen);*/
                pthread_mutex_lock(&clientesMutex);
                if (msg->multicast) {
                        //FIXME esto es de test aca se hce multicast
                        //test_rdt_send_broadcast(socEmisor, rdt_msg, msg->destino, msg->dest_puerto);
                        rdt_send_multicast(socEmisor, msg->msg, getClientesIdForMulticast());
                }
                else {
                        //FIXME esto es de test aca se hace unicast
                        //test_rdt_send(socEmisor, rdt_msg, msg->destino, msg->dest_puerto);
                        rdt_sendto(socEmisor, msg->msg, msg->destino, msg->dest_puerto);
                }
                pthread_mutex_unlock(&clientesMutex);
                cantMensajes++;

        }
        return NULL;
}


void* receptorMensajes(void*) {

        while (true) {
                //FIXME esto es un test, aca va rdt_rvc
                //appMsg* msg = test_rdt_rcv(socReceptor);
                char* ipEmisor;
                int puertoEmisor;
                char* msg = rdt_recibe(socReceptor, ipEmisor, puertoEmisor);

                MsgComand command = getCommandFromMsg(msg);

                switch (command) {
                        case COM_LOGIN:
                                processLoginMsg(ipEmisor, puertoEmisor, msg);
                                break;
                        case COM_GET_CONNECTED:
                                processGetConnectedMsg(ipEmisor, puertoEmisor);
                                break;
                        case COM_MSG:
                                processMulticastMessage(ipEmisor, msg);
                                break;
                        case COM_PVT_MSG:
                                processPrivatetMessage(ipEmisor, msg);
                                break;
                        case COM_LOGOUT:
                                processLogut(ipEmisor, puertoEmisor);
                                break;
                        case COM_INVALID:
                                //TODO ver que se hace con un caracter valido
                                break;
                }
        }
        return NULL;
}


/*
 *
 */

 void* monitorClientes(void* ) {

         while (true) {
                 sleep(MONITOR_TIME);
                 pthread_mutex_lock(&clientesMutex);
                 map<string, Cliente*>::iterator iter = Clientes->begin();
                 queue<string> aBorrar;
                 while (iter != Clientes->end()) {
                         Cliente* c = iter -> second;
                         time_t now = time(0);
                         double dif = difftime(now, c->ult_actividad);

                         if (dif > TTL_CLIENTES) {
                                 aBorrar.push(iter->first);
                         }
                         iter++;
                 }

                 while (!aBorrar.empty()) {
                         string ip = aBorrar.front();
                         aBorrar.pop();
                         Cliente* c  = Clientes->at(ip);
                         Clientes->erase(ip);
                         Mensaje * mensaje = crearMensaje(c->ip, c->puerto, false, GOODBYE);
                         encolarMensaje(mensaje);
                 }
                 pthread_mutex_unlock(&clientesMutex);

         }
         return NULL;
 }


void init() {

        //test_init();
        pthread_mutex_init(&queueMutex, NULL);
        pthread_cond_init (&emitCond, NULL);
        pthread_mutex_init(&clientesMutex, NULL);

}

int main(int argc, char** argv) {

        init();

        //FIXME aca no tengo claro que pasarle.
        std::cout << "socEmisor" << std::endl;
        socEmisor = CrearSocket(9999, false);
        std::cout << "socReceptor" << std::endl;
        socReceptor = CrearSocket(puertoServidor, false);


        pthread_t receptorHilo;
        pthread_create(&receptorHilo, NULL, receptorMensajes, NULL);

        pthread_t emisorHilo;
        pthread_create(&emisorHilo, NULL, emisorMensajes, NULL);

        pthread_t monitorHilo;
        pthread_create(&monitorHilo, NULL, monitorClientes, NULL);

        consola();

        return 0;
}
