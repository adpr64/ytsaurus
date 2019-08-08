package ru.yandex.yt.ytclient.proxy;

import java.util.concurrent.CompletableFuture;

import ru.yandex.inside.yt.kosher.common.GUID;
import ru.yandex.inside.yt.kosher.ytree.YTreeNode;
import ru.yandex.yt.rpcproxy.TCheckPermissionResult;
import ru.yandex.yt.ytclient.proxy.request.CheckPermission;
import ru.yandex.yt.ytclient.proxy.request.ConcatenateNodes;
import ru.yandex.yt.ytclient.proxy.request.CopyNode;
import ru.yandex.yt.ytclient.proxy.request.CreateNode;
import ru.yandex.yt.ytclient.proxy.request.ExistsNode;
import ru.yandex.yt.ytclient.proxy.request.GetNode;
import ru.yandex.yt.ytclient.proxy.request.LinkNode;
import ru.yandex.yt.ytclient.proxy.request.ListNode;
import ru.yandex.yt.ytclient.proxy.request.MoveNode;
import ru.yandex.yt.ytclient.proxy.request.ReadFile;
import ru.yandex.yt.ytclient.proxy.request.ReadTable;
import ru.yandex.yt.ytclient.proxy.request.RemoveNode;
import ru.yandex.yt.ytclient.proxy.request.SetNode;
import ru.yandex.yt.ytclient.proxy.request.StartOperation;
import ru.yandex.yt.ytclient.proxy.request.WriteFile;
import ru.yandex.yt.ytclient.proxy.request.WriteTable;
import ru.yandex.yt.ytclient.wire.UnversionedRowset;
import ru.yandex.yt.ytclient.wire.VersionedRowset;

public interface TransactionalClient {
    CompletableFuture<UnversionedRowset> lookupRows(LookupRowsRequest request);

    CompletableFuture<VersionedRowset> versionedLookupRows(LookupRowsRequest request);

    CompletableFuture<UnversionedRowset> selectRows(SelectRowsRequest request);

    CompletableFuture<GUID> createNode(CreateNode req);

    CompletableFuture<Void> removeNode(RemoveNode req);

    CompletableFuture<Void> setNode(SetNode req);

    CompletableFuture<YTreeNode> getNode(GetNode req);

    CompletableFuture<YTreeNode> listNode(ListNode req);

    CompletableFuture<GUID> copyNode(CopyNode req);

    CompletableFuture<GUID> linkNode(LinkNode req);

    CompletableFuture<GUID> moveNode(MoveNode req);

    CompletableFuture<Boolean> existsNode(ExistsNode req);

    CompletableFuture<Void> concatenateNodes(ConcatenateNodes req);

    CompletableFuture<TableReader> readTable(ReadTable req);

    CompletableFuture<TableWriter> writeTable(WriteTable req);

    CompletableFuture<FileReader> readFile(ReadFile req);

    CompletableFuture<FileWriter> writeFile(WriteFile req);

    CompletableFuture<Void> concatenateNodes(String [] from, String to);

    CompletableFuture<GUID> startOperation(StartOperation req);

    CompletableFuture<TCheckPermissionResult> checkPermission(CheckPermission req);
}
