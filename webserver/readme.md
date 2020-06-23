# Webserver Usage

It's really easy to use the webserver! All it takes is just a simple command to run the webserver:

```bash
sudo python3 server.py
```

**Note: If you're not on Linux, you can omit the `sudo`**

Once the command is run, the host thread will automatically start a HTTP server on a separate thread. To interact with the webserver, simply use one of the following commands:

| **Command**        | **Summary**                                                           | **Arguments**                                                                                         |
| ------------------ | --------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `gen_caseid {num}` | Generates and saves a given number of CaseIDs used for TEK submission | - `num` (Optional): The number of CaseIDs to generate. If omitted, the server will just generate one. |
| `list_caseid`      | Lists the internal CaseID array                                       | N/A                                                                                                   |
| `get_caseid`       | Loads the CaseID array from the copy on the hard disk.                | N/A                                                                                                   |
| `commit_caseid`    | Saves the CaseID array onto the hard disk                             | N/A                                                                                                   |
| `commit_teks`      | Saves the pending TEK array onto the hard disk                        | N/A                                                                                                   |
| `list_teks`        | Lists the array of pending TEKs                                       | N/A                                                                                                   |
| `help`             | Prints a list of available commands                                   | N/A                                                                                                   |
| `exit`             | Forcibly quits the program.                                           | N/A                                                                                                   |


