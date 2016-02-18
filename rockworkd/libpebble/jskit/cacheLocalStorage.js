//Since we don't have JS 6 support, this hack will allow us to save changes to localStorage when using dot or square bracket notation

for (var key in localStorage) {
    _jskit.localstorage.setItem(key, localStorage.getItem(key));
}

for (var key in _jskit.localstorage.keys()) {
    if (localStorage[key] === undefined) {
        _jskit.localstorage.removeItem(key);
    }
}
