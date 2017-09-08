#include <mapreduce/yt/interface/client.h>
#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/examples/tutorial/simple_map_protobuf/data.pb.h>

#include <util/stream/output.h>
#include <util/system/user.h>

using namespace NYT;

class TComputeEmailsMapper
    : public IMapper<TTableReader<TLoginRecord>, TTableWriter<TEmailRecord>>
{
public:
    virtual void Do(TReader* reader, TWriter* writer) override
    {
        for (; reader->IsValid(); reader->Next()) {
            const auto& loginRecord = reader->GetRow();

            TEmailRecord emailRecord;
            emailRecord.SetName(loginRecord.GetName());
            emailRecord.SetEmail(loginRecord.GetLogin() + "@yandex-team.ru");

            writer->AddRow(emailRecord);
        }
    }
};
REGISTER_MAPPER(TComputeEmailsMapper);

int main(int argc, const char** argv) {
    NYT::Initialize(argc, argv);

    TConfig::Get()->UseClientProtobuf = false; // Говорим библиотеке, что будем использовать «нативный» протобуф.

    auto client = CreateClient("freud");

    // Выходная табличка у нас будет лежать в tmp и содержать имя текущего пользователя.
    const TString outputTable = "//tmp/" + GetUsername() + "-tutorial-emails-protobuf";

    client->Map(
        TMapOperationSpec()
            .AddInput<TNode>("//home/ermolovd/yt-tutorial/staff_unsorted")
            .AddOutput<TNode>(outputTable),
        new TComputeEmailsMapper);

    Cout << "Output table: " << outputTable << Endl;

    return 0;
}

