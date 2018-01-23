WorkerScript.onMessage = function(message) {
    var messages = message.messages;
    var model = message.model;

    if (message.op === 'replace') {
        model.clear();
        messages.forEach(function(m) {
            model.append(m);
        });
    }
    else if (message.op === 'prepend') {
        messages.reverse().forEach(function(m) {
            model.insert(0, m);
        });
    }

    model.sync();

    WorkerScript.sendMessage({
        op: message.op
    });
}
