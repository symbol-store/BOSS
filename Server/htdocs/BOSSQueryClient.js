//JS module retrieving data from BOSS
//it manages UrlQueries and support graphic queries

export class BOSSConnectionConfig{
    constructor(myBOSSServerUrl, myBOSSRestEndPoint) {
        this.BOSSServerUrl = myBOSSServerUrl;
        this.BOSSRestEndPoint = myBOSSRestEndPoint;
        this.BOSSRestUrl = this.BOSSServerUrl + "/" + this.BOSSRestEndPoint + "/";
    }
}

export function drawBOSSChart(drawFunction, updateFunction, queryUrl, queryInterval, config) {
    BOSSQuery(queryUrl, config).then((data) => {
        drawFunction(data);
    }).then(() => {
        setInterval(function () {
            BOSSQuery(queryUrl, config).then((data) => {
                updateFunction(data);
            });
        }, queryInterval);
    });
}

export async function BOSSQuery(urlQuery, config) {
    if (urlQuery == "") {
        return new Promise((resolve, reject) => {
            reject("empty urlQuery.");
        });
    }
    var url = config.BOSSRestUrl + urlQuery;
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