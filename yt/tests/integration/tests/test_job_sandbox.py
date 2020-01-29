from yt_env_setup import YTEnvSetup, patch_porto_env_only
from yt_commands import *

import yt.environment.init_operation_archive as init_operation_archive
from yt.yson import *
from yt.test_helpers import assert_items_equal, are_almost_equal

from flaky import flaky

import pytest
import time
import datetime

porto_delta_node_config = {
    "exec_agent": {
        "slot_manager": {
            # <= 18.4
            "enforce_job_control": True,
            "job_environment": {
                # >= 19.2
                "type": "porto",
            },
        }
    }
}

##################################################################

class TestSandboxTmpfs(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1
    REQUIRE_YTSERVER_ROOT_PRIVILEGES = True

    @authors("ignat")
    def test_simple(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            command="cat; echo 'content' > tmpfs/file; ls tmpfs/ >&2; cat tmpfs/file >&2;",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 1024 * 1024,
                    "tmpfs_path": "tmpfs",
                },
                "max_failed_job_count" : 1
            })

        jobs_path = op.get_path() + "/jobs"
        assert get(jobs_path + "/@count") == 1
        content = read_file(jobs_path + "/" + ls(jobs_path)[0] + "/stderr")
        words = content.strip().split()
        assert ["file", "content"] == words

    @authors("ignat")
    def test_custom_tmpfs_path(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            command="cat; echo 'content' > my_dir/file; ls my_dir/ >&2; cat my_dir/file >&2;",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 1024 * 1024,
                    "tmpfs_path": "my_dir",
                }
            })

        jobs_path = op.get_path() + "/jobs"
        assert get(jobs_path + "/@count") == 1
        content = read_file(jobs_path + "/" + ls(jobs_path)[0] + "/stderr")
        words = content.strip().split()
        assert ["file", "content"] == words

    @authors("ignat")
    def test_dot_tmpfs_path(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            command="cat; mkdir my_dir; echo 'content' > my_dir/file; ls my_dir/ >&2; cat my_dir/file >&2;",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 1024 * 1024,
                    "tmpfs_path": ".",
                }
            })

        jobs_path = op.get_path() + "/jobs"
        assert get(jobs_path + "/@count") == 1
        content = read_file(jobs_path + "/" + ls(jobs_path)[0] + "/stderr")
        words = content.strip().split()
        assert ["file", "content"] == words

        create("file", "//tmp/test_file")
        write_file("//tmp/test_file", "".join(["0"] * (1024 * 1024 + 1)))
        map(command="cat",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 1024 * 1024,
                    "tmpfs_path": ".",
                    "file_paths": ["//tmp/test_file"]
                }
            })

        map(command="cat",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 1024 * 1024,
                    "tmpfs_path": "./",
                    "file_paths": ["//tmp/test_file"]
                }
            })

        script = "#!/usr/bin/env python\n"\
                 "import sys\n"\
                 "sys.stdout.write(sys.stdin.read())\n"\
                 "with open('test_file', 'w') as f: f.write('Hello world!')"
        create("file", "//tmp/script")
        write_file("//tmp/script", script)
        set("//tmp/script/@executable", True)

        map(command="./script.py",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 100 * 1024 * 1024,
                    "tmpfs_path": ".",
                    "copy_files": True,
                    "file_paths": ["//tmp/test_file", to_yson_type("//tmp/script", attributes={"file_name": "script.py"})]
                }
            })

        with pytest.raises(YtError):
            map(command="cat; cp test_file local_file;",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_size": 1024 * 1024,
                        "tmpfs_path": ".",
                        "file_paths": ["//tmp/test_file"]
                    },
                    "max_failed_job_count": 1,
                })

        op = map(
            command="cat",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 1024 * 1024 + 10000,
                    "tmpfs_path": ".",
                    "file_paths": ["//tmp/test_file"],
                    "copy_files": True,
                },
                "max_failed_job_count": 1,
            })

        statistics = get(op.get_path() + "/@progress/job_statistics")
        tmpfs_size = get_statistics(statistics, "user_job.tmpfs_volumes.0.max_size.$.completed.map.sum")
        assert 0.9 * 1024 * 1024 <= tmpfs_size <= 1.1 * 1024 * 1024

        with pytest.raises(YtError):
            map(command="cat",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_size": 1024 * 1024,
                        "tmpfs_path": ".",
                        "file_paths": ["//tmp/test_file"],
                        "copy_files": True,
                    },
                    "max_failed_job_count": 1,
                })

    @authors("ignat")
    def test_incorrect_tmpfs_path(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        with pytest.raises(YtError):
            map(command="cat", in_="//tmp/t_input", out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_size": 1024 * 1024,
                        "tmpfs_path": "../",
                    }
                })

        with pytest.raises(YtError):
            map(command="cat", in_="//tmp/t_input", out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_size": 1024 * 1024,
                        "tmpfs_path": "/tmp",
                    }
                })


    @authors("ignat")
    def test_tmpfs_remove_failed(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        with pytest.raises(YtError):
            map(command="cat; rm -rf tmpfs",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_size": 1024 * 1024,
                        "tmpfs_path": "tmpfs",
                    },
                    "max_failed_job_count": 1
                })

    @authors("ignat")
    def test_tmpfs_size_limit(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        with pytest.raises(YtError):
            map(command="set -e; cat; dd if=/dev/zero of=tmpfs/file bs=1100000 count=1",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_size": 1024 * 1024
                    },
                    "max_failed_job_count": 1
                })

    @authors("ignat")
    def test_memory_reserve_and_tmpfs(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            command="python -c 'import time; x = \"0\" * (200 * 1000 * 1000); time.sleep(2)'",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_path": "tmpfs",
                    "memory_limit": 250 * 1000 * 1000
                },
                "max_failed_job_count": 1
            })

        assert get(op.get_path() + "/@progress/jobs/aborted/total") == 0

    @authors("psushin")
    def test_inner_files(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        create("file", "//tmp/file.txt")
        write_file("//tmp/file.txt", "{trump = moron};\n")

        op = map(
            command="cat; cat ./tmpfs/trump.txt",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            file=['<file_name="./tmpfs/trump.txt">//tmp/file.txt'],
            spec={
                "mapper": {
                    "tmpfs_path": "tmpfs",
                    "tmpfs_size": 1024 * 1024,
                },
                "max_failed_job_count": 1
            })

        assert get("//tmp/t_output/@row_count".format(op.id)) == 2

    @authors("ignat")
    def test_multiple_tmpfs_volumes(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            command=
                "cat; "
                "echo 'content_1' > tmpfs_1/file; ls tmpfs_1/ >&2; cat tmpfs_1/file >&2;"
                "echo 'content_2' > tmpfs_2/file; ls tmpfs_2/ >&2; cat tmpfs_2/file >&2;",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_volumes": [
                        {
                            "size": 1024 * 1024,
                            "path": "tmpfs_1",
                        },
                        {
                            "size": 1024 * 1024,
                            "path": "tmpfs_2",
                        },
                    ]
                },
                "max_failed_job_count": 1
            })

        jobs_path = op.get_path() + "/jobs"
        assert get(jobs_path + "/@count") == 1
        content = read_file(jobs_path + "/" + ls(jobs_path)[0] + "/stderr")
        words = content.strip().split()
        assert ["file", "content_1", "file", "content_2"] == words

    @authors("ignat")
    def test_incorrect_multiple_tmpfs_volumes(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        with pytest.raises(YtError):
            map(
                command="cat",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_volumes": [
                            {
                                "path": "tmpfs",
                            },
                        ]
                    },
                    "max_failed_job_count": 1
                })

        with pytest.raises(YtError):
            map(
                command="cat",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_volumes": [
                            {
                                "size": 1024 * 1024,
                            },
                        ]
                    },
                    "max_failed_job_count": 1
                })

        with pytest.raises(YtError):
            map(
                command="cat",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_volumes": [
                            {
                                "path": "tmpfs",
                                "size": 1024 * 1024,
                            },
                            {
                                "path": "tmpfs/inner",
                                "size": 1024 * 1024,
                            },
                        ]
                    },
                    "max_failed_job_count": 1
                })

        with pytest.raises(YtError):
            map(
                command="cat",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "tmpfs_volumes": [
                            {
                                "path": "tmpfs/fake_inner/../",
                                "size": 1024 * 1024,
                            },
                            {
                                "path": "tmpfs/inner",
                                "size": 1024 * 1024,
                            },
                        ]
                    },
                    "max_failed_job_count": 1
                })

    @authors("ignat")
    def test_multiple_tmpfs_volumes_with_common_prefix(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            command=
                "cat; "
                "echo 'content_1' > tmpfs_dir/file; ls tmpfs_dir/ >&2; cat tmpfs_dir/file >&2;"
                "echo 'content_2' > tmpfs_dir_fedor/file; ls tmpfs_dir_fedor/ >&2; cat tmpfs_dir_fedor/file >&2;",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_volumes": [
                        {
                            "size": 1024 * 1024,
                            "path": "tmpfs_dir",
                        },
                        {
                            "size": 1024 * 1024,
                            "path": "tmpfs_dir_fedor",
                        },
                    ]
                },
                "max_failed_job_count": 1
            })

        jobs_path = op.get_path() + "/jobs"
        assert get(jobs_path + "/@count") == 1
        content = read_file(jobs_path + "/" + ls(jobs_path)[0] + "/stderr")
        words = content.strip().split()
        assert ["file", "content_1", "file", "content_2"] == words

    @authors("ignat")
    def test_vanilla(self):
        op = vanilla(
            spec={
                "tasks": {
                    "a": {
                        "job_count": 2,
                        "command": 'sleep 5',
                        "tmpfs_volumes": [
                        ]
                    },
                    "b": {
                        "job_count": 1,
                        "command": 'sleep 10',
                        "tmpfs_volumes": [
                            {
                                "path": "tmpfs",
                                "size": 1024 * 1024,
                            },
                        ]
                    },
                    "c": {
                        "job_count": 3,
                        "command": 'sleep 15',
                        "tmpfs_volumes": [
                            {
                                "path": "tmpfs",
                                "size": 1024 * 1024,
                            },
                            {
                                "path": "other_tmpfs",
                                "size": 1024 * 1024,
                            },
                        ]
                    },
                },
            })

##################################################################

class TestSandboxTmpfsOverflow(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1
    USE_DYNAMIC_TABLES = True
    REQUIRE_YTSERVER_ROOT_PRIVILEGES = True

    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "slot_manager": {
                "job_environment": {
                    "type": "cgroups",
                    "memory_watchdog_period": 100,
                    "supported_cgroups": ["cpuacct", "blkio", "cpu"],
                },
            },
            "statistics_reporter": {
                "enabled": True,
                "reporting_period": 10,
                "min_repeat_delay": 10,
                "max_repeat_delay": 10,
            },
            "job_controller": {
                "resource_limits": {
                    "memory": 6 * 1024 ** 3,
                }
            },
        },
    }

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "enable_job_reporter": True,
            "enable_job_spec_reporter": True,
            "enable_job_stderr_reporter": True,
        }
    }

    def setup(self):
        sync_create_cells(1)
        init_operation_archive.create_tables_latest_version(self.Env.create_native_client(), override_tablet_cell_bundle="default")
        self._tmpdir = create_tmpdir("jobids")

    @authors("ignat")
    def test_multiple_tmpfs_overflow(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            track=False,
            command=with_breakpoint(
                "dd if=/dev/zero of=tmpfs_1/file  bs=1M  count=2048; ls tmpfs_1/ >&2; "
                "dd if=/dev/zero of=tmpfs_2/file  bs=1M  count=2048; ls tmpfs_2/ >&2; "
                "BREAKPOINT; "
                "python -c 'import time; x = \"A\" * (200 * 1024 * 1024); time.sleep(100);'"),
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_volumes": [
                        {
                            "size": 2 * 1024 ** 3,
                            "path": "tmpfs_1",
                        },
                        {
                            "size": 2 * 1024 ** 3,
                            "path": "tmpfs_2",
                        },
                    ],
                    "memory_limit": 4 * 1024 ** 3 + 200 * 1024 * 1024,
                },
                "max_failed_job_count": 1
            })

        wait(lambda: op.get_state() == "running")

        jobs = wait_breakpoint(timeout=datetime.timedelta(seconds=300))
        assert len(jobs) == 1
        job = jobs[0]

        def get_tmpfs_size():
            job_info = get_job(op.id, job)
            try:
                sum = 0
                for key, value in job_info["statistics"]["user_job"]["tmpfs_volumes"].iteritems():
                    sum += value["max_size"]["sum"]
                return sum
            except KeyError:
                print_debug("JOB_INFO", job_info)
                return 0

        wait(lambda: get_tmpfs_size() >= 4 * 1024 ** 3)

        assert op.get_state() == "running"

        release_breakpoint()

        wait(lambda: op.get_state() == "failed")

        assert op.get_error().contains_code(1200)

##################################################################

@patch_porto_env_only(TestSandboxTmpfs)
class TestSandboxTmpfsPorto(YTEnvSetup):
    DELTA_NODE_CONFIG = porto_delta_node_config
    USE_PORTO_FOR_SERVERS = True

##################################################################

class TestDisabledSandboxTmpfs(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "slot_manager": {
                "enable_tmpfs": False
            }
        }
    }

    @authors("ignat")
    def test_simple(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        op = map(
            command="cat; echo 'content' > tmpfs/file; ls tmpfs/ >&2; cat tmpfs/file >&2;",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "tmpfs_size": 1024 * 1024,
                    "tmpfs_path": "tmpfs",
                }
            })

        jobs_path = op.get_path() + "/jobs"
        assert get(jobs_path + "/@count") == 1
        content = read_file(jobs_path + "/" + ls(jobs_path)[0] + "/stderr")
        words = content.strip().split()
        assert ["file", "content"] == words


##################################################################

class TestFilesInSandbox(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 5
    NUM_SCHEDULERS = 1

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "static_orchid_cache_update_period": 100,
        }
    }

    @authors("ignat")
    @flaky(max_runs=3)
    def test_operation_abort_with_lost_file(self):
        create("file", "//tmp/script", attributes={"replication_factor": 1, "executable": True})
        write_file("//tmp/script", "#!/bin/bash\ncat")

        chunk_id = get_singular_chunk_id("//tmp/script")

        replicas = get("#{0}/@stored_replicas".format(chunk_id))
        assert len(replicas) == 1
        replica_to_ban = str(replicas[0]) # str() is for attribute stripping.

        banned = False
        for node in ls("//sys/cluster_nodes"):
            if node == replica_to_ban:
                set("//sys/cluster_nodes/{0}/@banned".format(node), True)
                banned = True
        assert banned

        wait(lambda: get("#{0}/@replication_status/default/lost".format(chunk_id)))

        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})
        op = map(track=False,
                 command="./script",
                 in_="//tmp/t_input",
                 out="//tmp/t_output",
                 spec={
                     "mapper": {
                         "file_paths": ["//tmp/script"]
                     }
                 })

        wait(lambda: op.get_job_count("running") == 1)

        time.sleep(1)
        op.abort()

        wait(lambda: op.get_state() == "aborted" and are_almost_equal(get("//sys/scheduler/orchid/scheduler/cell/resource_usage/cpu"), 0))

##################################################################

class TestArtifactCacheBypass(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    @authors("babenko")
    def test_bypass_artifact_cache_for_file(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        create("file", "//tmp/file")
        write_file("//tmp/file", '{"hello": "world"}')
        map(command="cat file",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "file_paths": ["<bypass_artifact_cache=%true>//tmp/file"],
                    "output_format": "json"
                }
            })

        assert read_table("//tmp/t_output") == [{"hello": "world"}]

    @authors("babenko")
    def test_bypass_artifact_cache_for_table(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        create("table", "//tmp/table")
        write_table("//tmp/table", [{"hello": "world"}])
        map(command="cat table",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "mapper": {
                    "file_paths": ["<bypass_artifact_cache=%true;format=json>//tmp/table"],
                    "output_format": "json"
                }
            })

        assert read_table("//tmp/t_output") == [{"hello": "world"}]

##################################################################

@pytest.mark.skip_if('not porto_avaliable()')
class TestNetworkIsolation(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1
    DELTA_NODE_CONFIG = {
        "exec_agent": {
            "test_network": True,
            "slot_manager": {
                "job_environment" : {
                    "type" : "porto",
                },
            }
        }
    }

    REQUIRE_YTSERVER_ROOT_PRIVILEGES = True
    USE_PORTO_FOR_SERVERS = True

    @authors("gritukan")
    def test_create_network_project_map(self):
        create("network_project_map", "//tmp/n")

    @authors("gritukan")
    def test_network_project_in_spec(self):
        create_user("u1")
        create_user("u2")
        create_network_project("n")
        set("//sys/network_projects/n/@project_id", 0xdeadbeef)
        set("//sys/network_projects/n/@acl", [make_ace("allow", "u1", "use")])

        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")
        write_table("//tmp/t_input", {"foo": "bar"})

        # Non-existent network project. Job should fail.
        with pytest.raises(YtError):
            map(command="cat",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "network_project": "x"
                    }
                })

        # User `u2` is not allowed to use `n`. Job should fail.
        with pytest.raises(YtError):
            map(command="cat",
                in_="//tmp/t_input",
                out="//tmp/t_output",
                spec={
                    "mapper": {
                        "network_project": "n"
                    }
                },
                authenticated_user="u2")

        op = map(track=False,
                 command=with_breakpoint("echo $YT_NETWORK_PROJECT_ID >&2; hostname >&2; BREAKPOINT; cat"),
                 in_="//tmp/t_input",
                 out="//tmp/t_output",
                 spec={
                     "mapper": {
                         "network_project": "n"
                     }
                 },
                 authenticated_user="u1")

        job_id = wait_breakpoint()[0]
        network_project_id, hostname, _ = get_job_stderr(op.id, job_id).split('\n')
        assert network_project_id == str(0xdeadbeef)
        assert hostname.startswith("slot_")
        release_breakpoint()
        op.track()
