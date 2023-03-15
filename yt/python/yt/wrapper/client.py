from .config import get_config, set_option
from .client_helpers import create_class_method, are_signatures_equal
from . import client_api

import sys
from copy import deepcopy


def report_and_exit(diag):
    sys.stderr.write(
        """Found difference in signatures beteween YtClient and client_api.py
Error: {}

Most likely, you need to run:
$ ya make yt/python/yt/wrapper/bin
$ yt/python/yt/wrapper/bin/generate_client_impl/generate_client_impl
        yt/python/yt/wrapper/client_impl.py

If the problem persists, consider reporting it to YTADMINREQ.

Exiting.
""".format(diag))
    sys.exit(1)


try:
    try:
        from .client_impl_yandex import YtClient
    except ImportError:
        from .client_impl import YtClient

    for name in client_api.all_names:
        if not are_signatures_equal(getattr(YtClient, name), create_class_method(getattr(client_api, name))):
            report_and_exit("Difference in signature for {}".format(name))

except AttributeError as e:
    report_and_exit(str(e))


def create_client_with_command_params(client=None, **kwargs):
    """Creates new client with command params."""
    new_client = YtClient(config=deepcopy(get_config(client)))
    set_option("COMMAND_PARAMS", kwargs, new_client)
    return new_client


# Backward compatibility.
Yt = YtClient
