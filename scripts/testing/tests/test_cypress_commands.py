
import pytest

from yt_env_setup import YTEnvSetup
from yt_commands import *


class TestCypressCommands(YTEnvSetup):
    NUM_MASTERS = 3
    NUM_HOLDERS = 0

    def test_invalid_cases(self):
        # path not starting with /
        with pytest.raises(YTError): set('a', '20')

        # path starting with single /
        with pytest.raises(YTError): set('/a', '20')

        # empty path
        with pytest.raises(YTError): set('', '20')

        # empty token in path
        with pytest.raises(YTError): set('//tmp/a//tmp/b', '30')

        # change the type of root
        with pytest.raises(YTError): set('/', '[]')

        # set the root to the empty map
        # expect_error( set('/', '{}'))

        # remove the root
        with pytest.raises(YTError): remove('/')
        # get non existent child
        with pytest.raises(YTError): get('//tmp/b')

        # remove non existent child
        with pytest.raises(YTError): remove('//tmp/b')

    def test_list(self):
        set('//tmp/list', '[1;2;"some string"]')
        assert get('//tmp/list') == '[1;2;"some string"]'

        set('//tmp/list/+', '100')
        assert get('//tmp/list') == '[1;2;"some string";100]'

        set('//tmp/list/^0', '200')
        assert get('//tmp/list') == '[200;1;2;"some string";100]'

        set('//tmp/list/^0', '500')
        assert get('//tmp/list') == '[500;200;1;2;"some string";100]'

        set('//tmp/list/2^', '1000')
        assert get('//tmp/list') == '[500;200;1;1000;2;"some string";100]'

        set('//tmp/list/3', '777')
        assert get('//tmp/list') == '[500;200;1;777;2;"some string";100]'

        remove('//tmp/list/4')
        assert get('//tmp/list') == '[500;200;1;777;"some string";100]'

        remove('//tmp/list/4')
        assert get('//tmp/list') == '[500;200;1;777;100]'

        remove('//tmp/list/0')
        assert get('//tmp/list') == '[200;1;777;100]'

        set('//tmp/list/+', 'last')
        assert get('//tmp/list') == '[200;1;777;100;"last"]'

        set('//tmp/list/^0', 'first')
        assert get('//tmp/list') == '["first";200;1;777;100;"last"]'

    def test_map(self):
        set('//tmp/map', '{hello=world; list=[0;a;{}]; n=1}')
        assert get_py('//tmp/map') == {"hello":"world","list":[0,"a",{}],"n":1}

        set('//tmp/map/hello', 'not_world')
        assert get_py('//tmp/map') == {"hello":"not_world","list":[0,"a",{}],"n":1}

        set('//tmp/map/list/2/some', 'value')
        assert get_py('//tmp/map') == {"hello":"not_world","list":[0,"a",{"some":"value"}],"n":1}

        remove('//tmp/map/n')
        assert get_py('//tmp/map') ==  {"hello":"not_world","list":[0,"a",{"some":"value"}]}

        set('//tmp/map/list', '[]')
        assert get_py('//tmp/map') == {"hello":"not_world","list":[]}

        set('//tmp/map/list/+', '{}')
        set('//tmp/map/list/0/a', '1')
        assert get_py('//tmp/map') == {"hello":"not_world","list":[{"a":1}]}

        set('//tmp/map/list/^0', '{}')
        set('//tmp/map/list/0/b', '2')
        assert get_py('//tmp/map') == {"hello":"not_world","list":[{"b":2},{"a":1}]}

        remove('//tmp/map/hello')
        assert get_py('//tmp/map') == {"list":[{"b":2},{"a":1}]}

        remove('//tmp/map/list')
        assert get_py('//tmp/map') == {}


    def test_attributes(self):
        set('//tmp/t', '<attr=100;mode=rw> {nodes=[1; 2]}')
        assert get('//tmp/t/@attr') == '100'
        assert get('//tmp/t/@mode') == '"rw"'

        remove('//tmp/t/@')
        with pytest.raises(YTError): get('//tmp/t/@attr')
        with pytest.raises(YTError): get('//tmp/t/@mode')

        # changing attributes
        set('//tmp/t/a', '< author=ignat > []')
        assert get('//tmp/t/a') == '[]'
        assert get('//tmp/t/a/@author') == '"ignat"'

        set('//tmp/t/a/@author', '"not_ignat"')
        assert get('//tmp/t/a/@author') == '"not_ignat"'

        # nested attributes (actually shows <>)
        set('//tmp/t/b', '<dir = <file = <>-100> #> []')
        assert get('//tmp/t/b/@dir/@') == '{"file"=<>-100}'
        assert get('//tmp/t/b/@dir/@file') == '<>-100'
        assert get('//tmp/t/b/@dir/@file/@') == '{}'

        # set attributes directly
        set('//tmp/t/@', '{key1=value1;key2=value2}')
        assert get('//tmp/t/@key1') == '"value1"'
        assert get('//tmp/t/@key2') == '"value2"'



