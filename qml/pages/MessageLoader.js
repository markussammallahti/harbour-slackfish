WorkerScript.onMessage = function(message) {
    var messages = message.messages;
    var model = message.model;

    if (message.op === 'replace') {
        model.clear();
        messages.forEach(function(m) {
            model.append(m);
        });
    }

    model.sync();

    WorkerScript.sendMessage({
        op: 'replace'
    });
}
