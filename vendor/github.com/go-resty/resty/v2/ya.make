GO_LIBRARY()

LICENSE(MIT)

SRCS(
    client.go
    digest.go
    middleware.go
    redirect.go
    request.go
    response.go
    resty.go
    retry.go
    trace.go
    transport.go
    transport_other.go
    util.go
)

GO_TEST_SRCS(
    client_test.go
    context_test.go
    middleware_test.go
    request_test.go
    resty_test.go
    retry_test.go
    util_test.go
)

GO_XTEST_SRCS(example_test.go)

END()

RECURSE(
    gotest
)
