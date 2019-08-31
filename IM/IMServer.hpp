#pragma once
#include <iostream>
#include <string>
#include <cstdio>
#include <sstream>
#include <signal.h>
#include "Util.hpp"
#include "mysql.h"
#include "mongoose.h"
using namespace std;
#define IM_DB "IM" 
#define MY_PORT 3306
#define SESSION_ID "im_sid"
#define SESSION_NAME "im_name"
#define NUM 1024
#define SESSION_TTL 1800.0
#define SESSION_CHECK_INTERVAL 5.0
struct mg_serve_http_opts s_http_server_opts;
typedef struct session
{
    uint64_t id;
    string name;
    double created;//用创建时间和最近一次使用时间来标记sessionID
    double last_used;
}session_t;
class Session
{
    private:
        session_t sessions[NUM];
    public:
        Session()
        {
            for(auto i = 0; i < NUM; i++)
            {
                sessions[i].id = 0;
                sessions[i].name = "";
                sessions[i].created = 0.0;
                sessions[i].last_used = 0.0;
            }
        }
        //判断是否登录过，只需判断cookie中有没有保存session id
        bool IsLogin(http_message* hm)
        {
            return GetSession(hm);
        }
        bool GetSession(http_message* hm)
        {
            uint64_t sid;
            char ssid[64];
            char *s_ssid = ssid;
            //从http请求中，获取cookie
            struct mg_str *cookie_header = mg_get_http_header(hm, "cookie");
            if(cookie_header == nullptr)
                return false;
            //将获取到的session id存在ssid中，利用ssid的长度判断session id是否为空，获取成功返回缓冲区的长度-1，负责返回0
            if(mg_http_parse_header2(cookie_header, SESSION_ID, &s_ssid, sizeof(ssid)) == 0)
                return false;
            
            sid = strtoull(ssid, NULL,10);
            for(auto i = 0; i < NUM; i++)
            {
                if(sessions[i].id == sid)
                {
                    sessions[i].last_used = mg_time();
                    return true;
                }
            }
            return false;
        }
        bool CreateSession(string name, uint64_t &id)
        {
            int i = 0;
            for(; i < NUM; i++)
            {
                if(sessions[i].id == 0)
                    break;
            }
            if(i == NUM)
            {
                return false;
            }
            //session id
            sessions[i].id = (uint64_t)(mg_time()*1000000L);
            sessions[i].name = name;
            sessions[i].last_used = sessions[i].created = mg_time();
            
            id = sessions[i].id;
            return true;
        }
        void DestroySession(session_t *s)
        {
            s->id = 0;
        }
        void CheckSession()
        {
            double threadhold = mg_time() - SESSION_TTL;
            for(auto i = 0; i < NUM; i++)
            {
                //session id 存在并且最后一次使用时间在死亡时间的左侧就销毁此id
                if(sessions[i].id > 0 && sessions[i].last_used < threadhold)
                {
                    //销毁第i个session id，sessions是数组的首地址
                    DestroySession(sessions+i);
                }
            }
        }
        ~Session()
        {

        }
};
class MysqlClient
{
    private:
        MYSQL* my;
    private:
        //1、连接数据库
        bool ConnectMysql()
        {
            my = mysql_init(NULL);
            cout << __LINE__ << endl;
            if(!mysql_real_connect(my, "localhost", "root", "123456", IM_DB, MY_PORT, NULL, 0))
            {
                cerr << "connect error" << endl;
                return false;
            }
            cout << "connect mysql success" << endl;
             if(0 != mysql_query(my, "set names 'utf8'"))
              {

                  cerr << "set names 'utf8' failed." << endl;
              }
            return true;
        }
    public:
        //在构造函数里面初始化MYSQL
        MysqlClient()
        {
        }
        //2、插入
        bool InsertUser(string name, string passwd)
        {
            ConnectMysql();
            //拼接sql串
            string sql = "INSERT INTO user (name, passwd) values(\"";
            sql += name;
            sql += "\",\"";
            sql += passwd;
            sql += "\")";
            cout << sql << endl;
            //插入 mysql_query(MYSQL *my, const char* p);第二个参数是要执行的sql语句
            int ret = mysql_query(my, sql.c_str());
            mysql_close(my);
            if(0 == ret)
            {
                return true;
            }
            return false;
        }
        bool SelectUser(string name, string passwd)
        {
            cout << "connect" << endl;
            ConnectMysql();
            string sql = "SELECT * FROM user WHERE name=\"";
            sql += name;
            sql += "\" AND passwd=\"";
            sql += passwd;
            sql += "\"";
            cout << sql << endl;
            if(mysql_query(my, sql.c_str()) != 0){
                mysql_close(my);
                cout << "error" << endl;
                return false;
            }
            //查询出来的结果保存在result中
            MYSQL_RES *result = mysql_store_result(my);
            int num = mysql_num_rows(result);
            free(result);
            mysql_close(my);
            return num > 0 ? true : false;
        }
        ~MysqlClient()
        {
           //mysql_close(my);
        }
};
class IMServer
{
    private:
        string port;
        //事件管理器,指向链表头部
        struct mg_mgr mgr;
        //listen socket对应的连接结构体,每个连接都会对应一个连接结构体
        struct mg_connection *nc;
        volatile bool quit = false;
        static MysqlClient mc;
        static Session sn;
    public:
        IMServer(string _port = "8080"):port(_port)
        {
        }
        static void Broadcast(struct mg_connection* nc, string msg)
        {
            struct mg_connection *c;
            //链表头部赋给c
            for(c = mg_next(nc->mgr, NULL);c != NULL; c = mg_next(nc->mgr, c))
            {
                //向特定连接发送消息
                mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT, msg.c_str(), msg.size());
            }
        }
        static void RegisterHandler(mg_connection* nc, int ev, void* data)
        {
            cout << "Register" << endl;
            if(ev == MG_EV_CLOSE)
                return;
            cout << "Register" << endl;
            string code = "0";
            string echo_json = "{\"result\": ";
            struct http_message *hm = (struct http_message*)data;
            cout << Util::MgStrToString(&(hm->body)) << endl;
            if(mg_vcmp(&hm->method, "POST") == 0)
            {
                string body = Util::MgStrToString(&hm->body);
                string name, passwd;
                if(Util::GetNameAndPasswd(body, name, passwd) && !name.empty() && !passwd.empty())
                {
                    if(mc.InsertUser(name, passwd))
                    {
                        code = "0";
                    }
                    else
                    {
                        code = "1";
                    }
                }
                else
                {
                    code = "2";
                }
                echo_json += code;
                echo_json += "}";
                mg_printf(nc, "HTTP/1.1 200 OK\r\n");
                mg_printf(nc, "Content-Length: %lu\r\n\r\n", echo_json.size());
                mg_printf(nc, echo_json.data());
            }
            else
            {
              mg_serve_http(nc, hm, s_http_server_opts);
            }
            nc->flags |= MG_F_SEND_AND_CLOSE;
        }
        //触发登录事件,拿到用户名和密码
        static void LoginHandler(mg_connection* nc, int ev, void *data)
        {
            if(ev == MG_EV_CLOSE)
                return;
            string code = "0";
            string echo_json = "{\"result\": ";
            string shead = "";
            //hm存整个http请求信息
            struct http_message* hm = (struct http_message*)data;
            cout << "loginhandler ev: " << ev << endl;
            mg_printf(nc, "HTTP/1.1 200 OK\r\n");
            //字符串比较函数mg_vcmp
            if(mg_vcmp(&hm->method, "POST") == 0)
            {
                string body = Util::MgStrToString(&hm->body);
                string name, passwd;
                //http请求后的用户名和密码以json串的形式存在body中，需要转成字符串提取出来去在数据库中查找
               // 获得用户名和密码且用户名和密码不为空
               if(Util::GetNameAndPasswd(body, name, passwd) && !name.empty() && !passwd.empty())
                {
                    cout << __LINE__ << endl;
                    if(mc.SelectUser(name, passwd))
                    {
                        uint64_t id = 0;
                        if(sn.CreateSession(name, id))
                        {
                            stringstream ss;
                            ss << "Set-Cookie: " << SESSION_ID << "=" << id << ";path=/\r\n";
                            ss << "Set-Cookie: " << SESSION_NAME << "=" << name << ";path=/\r\n";
                            shead = ss.str();
                            mg_printf(nc, shead.data());
                            code = "0";
                        }
                        else
                        {
                            code = "3";
                        }
                    }
                    else
                    {
                        code = "1";
                    }
                }
                else
                {
                    code = "2";
                }
                echo_json += code;
                echo_json += "}";
                mg_printf(nc, "Content-Length: %lu\r\n\r\n", echo_json.size());
                mg_printf(nc, echo_json.data());
            }
            else
            {
               mg_serve_http(nc, hm, s_http_server_opts);
            }
           //响应完毕，关闭连接
            nc->flags |= MG_F_SEND_AND_CLOSE;
        }
        //回调函数
        static void EventHandler(mg_connection* nc, int ev, void* data)
        {
            switch(ev)
            {
                case MG_EV_HTTP_REQUEST:
                    {
                        cout << "http request" << endl;
                        //获取整个http请求信息
                        struct http_message* hm = (struct http_message*)data;
                        string uri = Util::MgStrToString(&hm->uri);
                        cout << "debug: " << uri << endl;
                        if(uri.empty() || uri == "/" || uri == "/index.html")
                        {
                            if(sn.IsLogin(hm))
                            {
                                mg_serve_http(nc, hm, s_http_server_opts);
                            }
                            else
                            {
                                mg_http_send_redirect(nc, 302, mg_mk_str("/login.html"), mg_mk_str(NULL));
                            }
                        }
                        else
                        {
                            mg_serve_http(nc, hm, s_http_server_opts);
                        }
                        nc->flags |= MG_F_SEND_AND_CLOSE;
                    }
                    break;
                case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
                    {
                        //广播把消息发给所有人
                        Broadcast(nc, "some body join...");
                    }
                    break;
                //提取出发出去的websocket数据
                case MG_EV_WEBSOCKET_FRAME:
                    {
                        struct websocket_message *wm = (struct websocket_message*)data;
                        struct mg_str ms = {(const char*)wm->data, wm->size};
                        //把数据转成字符串
                        string msg = Util::MgStrToString(&ms);
                        Broadcast(nc, msg);
                    }
                    break;
                case MG_EV_CLOSE:
              //      cout << "link quit..." << endl;
                    break;
                case MG_EV_TIMER:
                    sn.CheckSession();
                    mg_set_timer(nc, mg_time()+SESSION_CHECK_INTERVAL);
                default:
            //        cout << "other ev:" << ev << endl;
                    break;
            }
        }
        void InitServer()
        {
            signal(SIGPIPE, SIG_IGN);
            mg_mgr_init(&mgr,NULL);
            nc = mg_bind(&mgr, port.c_str(), EventHandler);
            //注册对登录页面的处理动作
            mg_register_http_endpoint(nc, "/LH", LoginHandler);
            mg_register_http_endpoint(nc, "/RH", RegisterHandler);

            //设置监听连接为websocket
            mg_set_protocol_http_websocket(nc);
            s_http_server_opts.document_root = "web";

            mg_set_timer(nc, mg_time()+SESSION_CHECK_INTERVAL);
        }
        void Start()
        {
            int timeout = 1000000;
            while(!quit)
            {
                mg_mgr_poll(&mgr, timeout);
               // cout << "time out" << endl;
            }
        }
        ~IMServer()
        {
            mg_mgr_free(&mgr);
        }
};
MysqlClient IMServer::mc;
Session IMServer::sn;
