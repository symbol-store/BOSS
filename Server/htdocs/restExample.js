async function fetchRestData(url) {
    if (url == ""){
        return new Promise((resolve, reject) => {
            reject("empty url.");
        });
    }
    return new Promise((resolve, reject) => {
        fetch(url).then((res) => {
            res.json().then((data) => {
                data = JSON.stringify(data);
                resolve(data);
            },
                () => {
                    reject("request failed.");
                }
            );
        });
    });
}