from path_tools import escape_path, split_path, dirs
from http import make_request

import os
import string
import random

def get(path, check_errors=True, attributes=None):
    if attributes is None:
        attributes = []
    return make_request("GET", "get",
                        # Hacky way to pass attributes into url
                        dict(
                            [("path", escape_path(path))] +
                            [("attributes[%d]" % i, attributes[i]) for i in xrange(len(attributes))]
                        ),
                        #{"path": escape_path(path),
                        # "attributes": attributes},
                        check_errors=check_errors)

def set(path, value):
    return make_request("PUT", "set", {"path": escape_path(path)}, value)

def copy(source_path, destination_path):
    return make_request("POST", "copy",
                        {"source_path": escape_path(source_path),
                         "destination_path": escape_path(destination_path)})

def list(path):
    if not exists(path):
        # TODO(ignat):think about throwing exception here
        return []
    return make_request("GET", "list", {"path": escape_path(path)})

def exists(path):
    if path == "/":
        return True
    objects = get("/")
    cur_path = "/"
    for elem in split_path(path):
        if objects is None:
            objects = get(cur_path)
        if not isinstance(objects, dict) or elem not in objects:
            return False
        else:
            objects = objects[elem]
            cur_path = os.path.join(cur_path, elem)
    return True

def remove(path):
    if exists(path):
        return make_request("POST", "remove", {"path": escape_path(path)})
    # TODO(ignat):think about throwing exception here
    return None

def mkdir(path):
    create = False
    for dir in dirs(path):
        if not create and not exists(dir):
            create = True
        if create:
            set(dir, "{}")

def get_attribute(path, attribute, check_errors=True, default=None):
    if default is not None and attribute not in list_attributes(path):
        return default
    return get("%s/@%s" % (path, attribute), check_errors=check_errors)

def set_attribute(path, attribute, value):
    return set("%s/@%s" % (path, attribute), value)

def list_attributes(path, attribute_path=""):
    # TODO(ignat): it doesn't work now. We need support attributes in exists
    return list("%s/@%s" % (path, attribute_path))

def find_free_subpath(path):
    if not path.endswith("/") and not exists(path):
        return path
    LENGTH = 10
    char_set = string.ascii_lowercase + string.ascii_uppercase + string.digits
    while True:
        name = "%s%s" % (path, "".join(random.sample(char_set, LENGTH)))
        if not exists(name):
            return name
