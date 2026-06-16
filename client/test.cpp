#include<iostream>
#include<string>
#include<string.h>
#include<jsoncpp/json/json.h>

using namespace std;
int main()
{
    Json::Value val;

    val["name"]="小王";
    val["age"]=23;

    char buff[256]={0};

    strcpy(buff,val.toStyledString().c_str());
    cout<<"buff:"<<buff<<endl;
    //send(buff)
    
    //recv()
    Json::Value res;
    Json::Reader Read;
    Read.parse(buff,res);

    string name=res["name"].asString();
    int age=res["age"].asInt();

    cout<<"name:"<<name<<endl;
    cout<<"age:"<<age<<endl;
    
}