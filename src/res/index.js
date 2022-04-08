const tbl = document.getElementById('tbl').firstChild;
const search = document.getElementById('search');
const status = document.getElementById('status');

function debounce(func, timeout = 225) {
    let timer;
    return (...args) => {
        clearTimeout(timer);
        timer = setTimeout(() => { func.apply(this, args); }, timeout);
    };
}

function parseVocab(str) {
    const split = str.split('\n');
    res = [];
    for (let i = 0; i < split.length - 1; i += 2)
        res.push([split[i], split[i + 1]]);
    return res;
}

let lst = [];
let lsti = 0;

const obs = new IntersectionObserver(es => {
    const [e] = es;
    if (!e.isIntersecting)
        return;
    obs.disconnect();
    showlst(lsti + 500);
});

function showlst(i = 0) {
    lsti = i;
    if (i)
        tbl.innerHTML += lst.slice(lsti, lsti + 500).join('\n');
    else
        tbl.innerHTML = lst.slice(0, 500).join('\n');
    setTimeout(() => {
        if (lsti + 500 < lst.length)
            obs.observe(document.querySelector('#tbl tr:last-child'));
        status.innerHTML = `näytetään ${Math.min(lsti + 500, lst.length)}/${lst.length} osumaa`;
    });
}

(async () => {
    const vocab = parseVocab(await (await fetch('api/vocab')).text());
    search.readOnly = false;
    status.innerText = `ladattu ${vocab.length} alkiota`;
    ontype = debounce(e => {
        obs.disconnect();
        const re = new RegExp(e.value, 'i');
        lst = !e.value ? [] : vocab.filter(([w, _]) => re.exec(w)).map(([w, d]) => `<tr><td>${w}<td><p>${d}`);
        showlst();
    });
})().catch(e => {
    status.style.color = 'red';
    status.innerText = `virhe ladattaessa sisältöä: ${e}`;
    throw e;
});