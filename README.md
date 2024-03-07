# rio_experimentation
Experimentation with Win32 Registered I/O for UDP communication

All projects are tested and worked on Windows 11 (Version 10.0.22000.2057) with Visual studio 2022 (17.4.2).

## Projects
| Socket i/o | RIO with Events | Brief description                                     |
| ---------- | --------------- | ----------------------------------------------------- |
| recv       | recv_rio        | Receive packets on UDP port 0x4321 from any IPv4 host |
| send       | send_rio        | Send packets UDP packets to loalhost:0x4321           |

## Results
send_rio and recv_rio running  
![image](https://github.com/philippdiethelm/rio_experimentation/assets/97515731/4560fa6e-fc0b-4967-a9a1-da8ee0be7d0f)
![image](https://github.com/philippdiethelm/rio_experimentation/assets/97515731/70207ee4-0607-4f59-8c7f-63fc42abbb9c)
![image](https://github.com/philippdiethelm/rio_experimentation/assets/97515731/75144465-21b5-4689-87ae-9483a3eb0564)

## Tipps and Tricks

### Common error numbers
[Windows Sockets Error Codes](https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2)

| Code       | Value   | Description                                           |
| ---------- | ------- | ----------------------------------------------------- |
| WSAEINVAL  | 10022   | An invalid argument was supplied.                     |
| WSAENOBUFS | 10055   | No buffer space available.                            |

### Lookup error number from WSAGetLastError()
There is a powerful windows on-board method available to look up windows error messages from numbers:
`net helpmsg <number>`.

Usage  
```
C:\> net helpmsg 10022
An invalid argument was supplied.
```

## Links
- [SAC-593T: New Techniques to Develop Low Latency Network Apps](https://video.ch9.ms/build/2011/slides/SAC-593T_Briggs.pptx)
- [SAC-433T: Network acceleration and other NIC technologies for the data center](https://video.ch9.ms/build/2011/slides/SAC-433T_Stanwyck.pptx)

