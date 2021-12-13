# my_chatroom
使用poll实现的建议聊天室，支持多房间聊天功能
客户端程序使用poll同时监听用户输入和网络连接，并利用splice函数实现数据零拷贝，提高程序执行效率
服务器程序使用poll同时管理监听socket和连接socket，并用简单的map结构储存用户信息与房间号，实现分房间聊天功能
![image](https://github.com/gongke96/my_chatroom/blob/main/images/%E8%81%8A%E5%A4%A9%E5%AE%A4.png)
