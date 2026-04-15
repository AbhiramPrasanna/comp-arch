import numpy as np
import matplotlib.pyplot as plt

from pylib import file


class Drawer:
    def __init__(self):
        self.testcase_dict = {}
        self.testcase_dict_list = []

    def init_testcase(self, server, client_list, thread_list, workload_list):
        self.testcase_dict = {
            "server": server,
            "client_list": client_list,
            "thread_list": np.asarray(thread_list),  # dim1
            "workload_list": np.asarray(workload_list),  # dim2
            "throughput_matrix": np.ndarray((len(thread_list), len(workload_list)), np.float64),  # use to draw
            "latency_matrix": np.ndarray((len(thread_list), len(workload_list)), np.float64),  # use to draw
        }

    def save_this_testcase(self):
        self.testcase_dict_list.append(self.testcase_dict)

    def update_testcase_result(self, thread_index, workload_index, result):
        self.testcase_dict["throughput_matrix"][thread_index][workload_index] = result["all"]["throughput"]
        self.testcase_dict["latency_matrix"][thread_index][workload_index] = result["all"]["latency"]

    def draw_testcase_pictures(self, time_str: str):
        # dim 0: thread_list for different workload
        plt.clf()  # refresh
        pic = plt.axes()
        for p in range(len(self.testcase_dict["workload_list"])):
            y = self.testcase_dict["throughput_matrix"][:, p]
            pic.plot(
                self.testcase_dict["thread_list"], y,
                marker="o", label=f"workload={self.testcase_dict['workload_list'][p]}",
            )
        pic.set_xlabel("thread")
        pic.set_ylabel("throughput")
        plt.legend()
        plt.title(f"server={self.testcase_dict['server']}, clients={self.testcase_dict['client_list']}")
        pic.figure.savefig(file.generate_single_testcase_picture_filename("throughput_thread", time_str))

        plt.clf()
        pic = plt.axes()
        for p in range(len(self.testcase_dict["workload_list"])):
            y = self.testcase_dict["latency_matrix"][:, p]
            pic.plot(
                self.testcase_dict["thread_list"], y,
                marker="o", label=f"workload={self.testcase_dict['workload_list'][p]}",
            )
        pic.set_xlabel("thread")
        pic.set_ylabel("latency")
        plt.legend()
        plt.title(f"server={self.testcase_dict['server']}, clients={self.testcase_dict['client_list']}")
        pic.figure.savefig(file.generate_single_testcase_picture_filename("latency_thread", time_str))

    def draw_compare_testcases_pictures(self):
        if len(self.testcase_dict_list) <= 1:
            return

        compare_time = file.generate_time_str()

        # find the same workload & thread for all testcases

        common_workload_set = set(self.testcase_dict_list[0]["workload_list"])
        common_thread_set = set(self.testcase_dict_list[0]["thread_list"])
        for i in range(1, len(self.testcase_dict_list)):
            this_workload_set = set(self.testcase_dict_list[i]["workload_list"])
            this_thread_set = set(self.testcase_dict_list[i]["thread_list"])
            common_workload_set &= this_workload_set
            common_thread_set &= this_thread_set

        common_workload_list = list(common_workload_set)
        common_workload_list.sort()
        common_thread_list = list(common_thread_set)
        common_thread_list.sort()

        # get common data

        # common_thread_list = [2, 4, 8]
        # common_workload_list = [a, b, c, d]
        # throughput_list = [
        #   [  # server-client_list 1
        #      [1054322.0, 990595.46666667, 894920.66666667, 702000.8],
        #      [2076115.33333333, 1959232.66666667, 1742281.33333333, 1355087.33333333],
        #      [4070519.33333333, 3847078.66666667, 3330096.66666667, 2330798.0],
        #   ],
        #   [  # server-client_list 2
        #      [1054322.0, 990595.46666667, 894920.66666667, 702000.8],
        #      [2076115.33333333, 1959232.66666667, 1742281.33333333, 1355087.33333333],
        #      [4070519.33333333, 3847078.66666667, 3330096.66666667, 2330798.0],
        #   ],
        # ]

        throughput_list = np.ndarray(
            (
                len(self.testcase_dict_list),
                len(common_thread_list),
                len(common_workload_list),
            ),
            np.float64,
        )
        latency_list = np.ndarray(
            (
                len(self.testcase_dict_list),
                len(common_thread_list),
                len(common_workload_list),
            ),
            np.float64,
        )
        legend_name_list = [
            f"server={testcase['server']}, clients={testcase['client_list']}"
            for testcase in self.testcase_dict_list
        ]
        for p in range(len(common_workload_list)):
            for t in range(len(common_thread_list)):
                i = -1
                for testcase in self.testcase_dict_list:
                    i += 1
                    this_t = list(testcase["thread_list"]).index(common_thread_list[t])
                    this_p = list(testcase["workload_list"]).index(common_workload_list[p])
                    throughput_list[i][t][p] = testcase["throughput_matrix"][this_t][this_p]
                    latency_list[i][t][p] = testcase["latency_matrix"][this_t][this_p]
        common_workload_list = np.asarray(common_workload_list)
        common_thread_list = np.asarray(common_thread_list)

        # draw pictures

        # 2 pictures [
        #   {for all fix workload, different ser-cli lines} thread - throughput,
        #   {for all fix workload, different ser-cli lines} thread - latency,
        # ]

        # pictures 1, 2
        p = -1
        for workload in common_workload_list:
            p += 1

            plt.clf()
            pic = plt.axes()
            for i in range(len(legend_name_list)):
                y = throughput_list[i, :, p]
                pic.plot(common_thread_list, y, marker="o", label=legend_name_list[i])
            pic.set_xlabel("thread")
            pic.set_ylabel("throughput")
            plt.legend()
            plt.title(f"workload={workload}")
            pic.figure.savefig(
                file.generate_compare_testcases_picture_filename(
                    about="thread_throughput",
                    fix_value=f"workload_{workload}",
                    time_str=compare_time,
                )
            )

            plt.clf()
            pic = plt.axes()
            for i in range(len(legend_name_list)):
                y = latency_list[i, :, p]
                pic.plot(common_thread_list, y, marker="o", label=legend_name_list[i])
            pic.set_xlabel("thread")
            pic.set_ylabel("latency")
            plt.legend()
            plt.title(f"workload={workload}")
            pic.figure.savefig(
                file.generate_compare_testcases_picture_filename(
                    about="thread_latency",
                    fix_value=f"workload_{workload}",
                    time_str=compare_time,
                )
            )
