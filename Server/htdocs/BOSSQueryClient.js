//JS module retrieving data from BOSS
//it manages UrlQueries and support graphic queries

export class BOSSConnectionConfig {
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
        return setInterval(function () {
            BOSSQuery(queryUrl, config).then((data) => {
                updateFunction(data);
            });
        }, queryInterval);
    });
}

export function drawBOSSChartFromElementId(drawFunction, updateFunction, elementId, getQuery, queryInterval, config) {
    let queryUrl = getQuery(elementId);
    BOSSQuery(queryUrl, config).then((data) => {
        drawFunction(data);
    }).then(() => {
        return setInterval(function () {
            let queryUrl = getQuery(elementId);
            console.log(queryUrl);
            BOSSQuery(queryUrl, config).then((data) => {
                updateFunction(data);
            });
        }, queryInterval);
    });
}

export async function BOSSQuery(urlQuery, config) {
    if (urlQuery == "") {
        return new Promise((resolve, reject) => {
            var data = [{
                "query": urlQuery,
                "executed": "no"
            }]
            reject(JSON.stringify(data));
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
                    var data = [{
                        "query": urlQuery,
                        "executed": "no"
                    }]
                    reject(JSON.stringify(data));
                }
            );
        });
    });
}