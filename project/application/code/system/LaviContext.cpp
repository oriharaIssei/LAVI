#include "LaviContext.h"

#include "SharedMediaContext.h"

SharedMediaContext& LaviContext::Get() {
    // 関数ローカル static により初回アクセス時に構築、プログラム終了時に破棄される。
    static SharedMediaContext instance;
    return instance;
}
